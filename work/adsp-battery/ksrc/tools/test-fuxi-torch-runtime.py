import os
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_torch_runtime.h"
int main(void) {
    assert(fuxi_torch_caller_allowed(1,0,NULL));
    assert(fuxi_torch_caller_allowed(0,1,"SpringBoard"));
    assert(fuxi_torch_caller_allowed(0,1,"mediaserverd"));
    assert(!fuxi_torch_caller_allowed(0,0,"mediaserverd"));
    assert(!fuxi_torch_caller_allowed(0,1,"mediaserverdX"));
    assert(!fuxi_torch_caller_allowed(0,1,"mediaserver"));
    assert(!fuxi_torch_caller_allowed(0,1,"Mediaserverd"));
    assert(!fuxi_torch_caller_allowed(0,0,"SpringBoard"));
    assert(!fuxi_torch_caller_allowed(0,1,"SpringBoardX"));
    assert(!fuxi_torch_caller_allowed(0,1,"Spring"));
    assert(!fuxi_torch_caller_allowed(0,1,"springboard"));
    assert(!fuxi_torch_caller_allowed(0,1,NULL));
    assert(!fuxi_torch_caller_allowed(0,1,""));
    FuxiTorchRuntime state={0}, saved;
    assert(!fuxi_torch_requested(&state,0));
    assert(!fuxi_torch_request(&state,4,100));
    assert(fuxi_torch_requested(&state,2000000099ull)==4);
    assert(!fuxi_torch_requested(&state,2000000100ull));
    for(unsigned level=1;level<=4;level++) {
        assert(!fuxi_torch_request(&state,level,10000000000ull));
        assert(fuxi_torch_requested(&state,11000000000ull)==level);
        assert(!fuxi_torch_request(&state,level,11000000000ull));
        assert(fuxi_torch_requested(&state,12000000000ull)==level);
        assert(!fuxi_torch_requested(&state,13000000000ull));
        assert(!fuxi_torch_request(&state,level,0));
        assert(!fuxi_torch_request(&state,0,1));
        assert(!fuxi_torch_requested(&state,1));
    }
    saved=state;
    assert(fuxi_torch_request(&state,5,0)<0);
    assert(!memcmp(&state,&saved,sizeof(state)));
    assert(fuxi_torch_request(&state,1,UINT64_MAX-1)<0);
    assert(!memcmp(&state,&saved,sizeof(state)));
    assert(!fuxi_torch_request(&state,1,0));
    state.failed=1;
    assert(!fuxi_torch_requested(&state,1));
    assert(fuxi_torch_request(&state,1,2)<0);
    assert(!fuxi_torch_request(&state,0,3));
    assert(!fuxi_torch_request(&state,0,UINT64_MAX));
    assert(fuxi_torch_request(NULL,1,0)<0);
    puts("PASS: production torch caller admission and lease expiration, renewal, stop, overflow and latched failure");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fuxi-torch-runtime-') as temporary:
    directory = Path(temporary)
    cfile, binary = directory / 'runtime.c', directory / 'runtime'
    cfile.write_text(source)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'xnu/fuxi-adsp'), str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
