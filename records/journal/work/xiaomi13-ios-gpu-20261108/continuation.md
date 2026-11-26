# Xiaomi 13 iOS GPU continuation — 2026-11-08

Target serial <序號>; product fuxi. Earlier user confirmed the real iOS CPU-rendered home screen and swipe, with poor responsiveness. GPU home-screen operation remains unverified.

The 20 GiB GPU root image at %USERPROFILE%\AppData\Local\Temp\fuxi-gpu-20261108\ios16-gpu-root.img has SHA-256 2f12014a3f8f6a6200b094ee13b73c6615498c644276caea936b1c7aa02e227. qemu-img compare reported Images are identical against gpu-root-overlay.qcow2, which uses the original D:\xiaomi13\fuxi_ios\candidates\vm-tools\ios16-root-build.img as backing. This comparison is not device validation. Both image and overlay are in Temp and may be cleaned automatically.

Metal, Mesa and tool artifacts came from third-party GitHub Actions and their supplied checksum manifests matched; full signature trust and target compatibility remain unverified. The staging script enabled backboardd in the root allowlist; the separate fuxibbgpu=1 argument is absent unless explicitly added to a new boot candidate. Device qualification in D:\xiaomi13\fuxi_ios\ios-fuxi\docs-lanes\metal-m9j.md requires Mac frontend checks and sequential CPU fallback and real composition verification.

Android was booted following the prior iOS run, so all prior pinned-file physical extent mappings must be reacquired and validated in recovery. The existing /data/ios16-codex-b.img had been written by iOS; preserve it. Do not use old ADT/map or write_pinned_extents.c without checking current block topology, mount state, allocation, pinning, flags, logical coverage and disjointness.

This continuation: adb reported sys.boot_completed=1 before adb reboot bootloader. Fastboot reported product fuxi, unlocked yes. fastboot boot D:\xiaomi13\fuxi_ios\backup\recovery_a.img and fastboot boot D:\xiaomi13\fuxi_ios\twrp_trimmed.img each returned Failed to load/authenticate boot image: Bad Buffer Size. The latter was confirmed to be a byte-for-byte prefix of recovery_a.img. fastboot reboot recovery and fastboot reboot both returned OKAY, but USB enumeration subsequently still showed <序號> as bootloader fastboot (is-userspace no). No new phone-storage write or GPU boot was performed. Need physical-screen confirmation before diagnosing next step. D: had 0 free space during this work.

## Additional checks

Android v4 header parsing of both recovery_a.img and twrp_trimmed.img shows kernel_size=0 and ramdisk_size=76070660 bytes; the latter is a prefix of the former. The zero kernel gives a concrete reason they are unsuitable as standalone fastboot boot images. This does not establish the installed recovery's current bootability. The device was still in bootloader fastboot on recheck.

The downloaded FuxiMetal binary contains strings for fuxibbgpu, backboardd admission/fallback and HostImport preflight; the chosen kernel binary contains NativeAdrenoGpuPublished v2 HostImport. These static observations establish presence of code paths only. The Metal build log explicitly says the quick build skipped tools/test-fuxi-metal-frontend.py. Therefore the required native Mac frontend suite is not demonstrated by that artifact, and ABI/source pin matching is not a complete compatibility or safety test.

## Recovery continuation — 2026-11-08

ADB serial <序號> is currently in recovery as root. /dev/block/dm-8 is mounted rw as F2FS at /data and /sdcard; sysfs identifies sda32 as its slave, but a full device-mapper table and mapping semantics have not been established. /data had 106 GiB free. Candidate /data/ios16-gpu-c.img inode 157885, root:root mode 0600 and size 21474836480 was present; /tmp/inspect-fiemap reported F2FS pin flag 0x800000. CPU fallback /data/ios16-codex-b.img inode 152183 remains present. No image write or boot was performed in this continuation. Recheck all state and extent maps before any write or boot.

macOS native GateFrontendTests and the matching frontend suite remain unverified. This Windows machine reports no installed WSL distribution, and the authenticated GitHub account has READ access to bangprovn/ios-fuxi, insufficient to dispatch its workflow. Local source has 134 status entries and must not be published or rewritten without reviewing scope. Neither static code presence nor a manifest checksum substitutes for the required tests. Obtain a trusted Mac or an authorized CI runner at the matching source revision before GPU device qualification. Do not run raw writer until the mapping, encryption path, unmount coverage, candidate identity, bounds and read-back plan are established.

## Read-only mapping checks — 2026-11-08 continuation

A purpose-built read-only DM_TABLE_STATUS helper reports userdata has exactly one default-key target, sector start 0, length 469917144, backing block 259:16, backing offset 0. The helper prints no cryptographic parameters. This establishes the reported sector offset but not permission to access raw blocks or safety of arbitrary writes; default-key encryption is active.

A single non-destructive 4096-byte sample at candidate logical offset 0, corresponding to FIEMAP physical offset 1696595968, gave SHA-256 ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7 both through the file and dm-8, but 48c052f3af9dba4027cc8110a350fc4c3c11ee17240d2f93ba28c241c66c2000 through sda32. This shows ciphertext differs from the file data for that sample. Do not extrapolate one sample into a complete safety proof.

snapshot-fiemap.py acquired all four current maps: gpu-c 46 extents, cpu-b 160, other ios16 664 and ios27 357. Each covers its reported size, is aligned, has permitted flags and no internal or pairwise overlap; all four match the earlier post-allocation snapshots. This is a time-limited snapshot, not permission or proof safe to raw-write after state changes.

Artifact build log records source commit 4b8faded6e76a4730832144fc3cae34c4419fa51; GitHub confirms the commit exists in zgl97 and bangprovn forks, but no Actions runs for that SHA were returned in either queried fork. The locally available repository is at a different commit; the matching macOS frontend suite remains unverified.

## Cross-artifact source audit — 2026-11-08

The artifacts do not come from one source revision: kernel build log says 979e2f08c704677bb75934ae96d4686ba9d1692b, FuxiMetal says 4b8faded6e76a4730832144fc3cae34c4419fa51, Mesa says f44bf5693da18ac11abfa510c5ad711fc316254b, and tools says ed7b9f8cbe698ce8f23a80133b134fb586af8166. Public GitHub compare reports that kernel and Metal revisions diverged (142 ahead and 85 behind). This is source history, not a runtime compatibility verdict.

Retrieved NativeAdrenoABI.h directly at all four revisions. All have 9715 bytes and SHA-256 c34d36e636ce02119cad6702258aba997869d74eecc0f09a1b9b7c51d881cb03. The Mesa artifact build log independently reports the same ABI-header hash and an exact-patch-set check. The kernel and Metal sources both expose v2/HostImport in the inspected user-client source. Other internal source files differ, including kernel allocation and IRQ code, while the Mesa package includes a BO cache patch absent at the kernel revision. The BO cache patch says it retains kernel handles, uses ABI 2 and honors a TU_DARWIN_BO_CACHE=0 fallback; this does not prove its safety or correctness with the older kernel. The CI artifact checks are producer-provided evidence, not independent build provenance or real-device qualification.

Local audit copies are under abi-compare. The upstream GitHub compare response includes source path lists and is subject to provider pagination/limits; do not treat its 209 listed changed files as a complete diff. Native Mac frontend suite, full cross-version behavior and actual home-screen GPU composition remain unverified. Device still reports ADB recovery.

## Android Recovery model tests — 2026-11-08

The original source revision 4b8faded6e76a4730832144fc3cae34c4419fa51 was run through a local adapter using Android NDK 26.1 clang++ targeting aarch64-linux-android29. The adapter dispatches the upstream non-Darwin C++ tests through adb to <序號> in Recovery, under /tmp/fuxi-gpu-model-tests. Android differs from the upstream Linux and macOS environments; these are CPU model tests and do not execute the iOS frontend, kernel, GPU, or compositor. The upstream POSIX staging policy test was explicitly skipped, as were all native Objective-C/macOS tests.

A first ASan+UBSan run stopped at SurfaceLeaseTests with an illegal instruction and several vptr diagnostics. A minimal independent standard C++ std::function + std::async control, compiled with the same Android NDK flags, produced similar vptr diagnostics and failed with illegal instruction; disabling vptr alone still failed. The same control passed without sanitizers. The cause remains undiagnosed, so the sanitizer run is inconclusive and its diagnostics should not be attributed to the Fuxi code as a proven bug or dismissed as false positives.

The adapted run without any sanitizer completed 34 compiles and 108 test invocations with exit status zero. This includes synthetic BackboardGate, SurfaceLease and HostImportRange tests; any displayed GPU timings in that log are test-supplied values, not observed GPU execution. The independent HostImportRange test also completed with ASan+UBSan before the control failure was discovered; it covers arithmetic only. The tool adapter intentionally returns success for the skipped policy test to let the other tests proceed: the exit status is not evidence of that test passing. Full logs and adapters: android-model-tests.log, android-model-tests-nosan.log, run-android-model-tests.py, run-android-model-tests-nosan.py; ndk-sanitizer-control.cpp is the control. Neither the user-requested GPU rendering nor the required Mac frontend gate is established by these results.

Control follow-up: the independent std::function + std::async program failed with ASan alone (exit 132, illegal instruction) and with UBSan alone (exit 134 when halted on the first vptr diagnostic), whereas its unsanitized build returned result=42. This narrows the observation but does not establish the cause. No sanitizer-based suite pass is claimed.

## Proposed macOS CI route — 2026-11-08

Prepared mac-ci-proposal/frontend.yml for manual execution on a standard macos-15 GitHub-hosted runner in a new public repository under the authenticated user's account. It fetches only the public zgl97/ios-fuxi source at commit 4b8faded6e76a4730832144fc3cae34c4419fa51 and runs tools/test-fuxi-metal-frontend.py with job permissions set to none. It publishes workflow and logs only, not local source, images, or device data. YAML parsed locally and a separate shallow git fetch of the exact commit succeeded. The workflow itself has not run. Runner eligibility, availability and any charges have not been verified for this account. Creating a public repository is externally visible and requires user approval before proceeding. See mac-ci-proposal/README.md.

Additional Recovery sanitizer diagnosis: an ASan-only std::async control dies with SIGILL at offset 0x82fcc in the NDK ASan library writev interceptor, while an unsanitized build succeeds. SIGILL was observed in the standard-library control as well as the model test; this locates the failure but does not establish its cause or resolve the separate UBSan vptr warnings.


## Kernel/BO-cache compatibility audit — 2026-11-08

The pinned 979e2f kernel's NativeAdreno user client and the Metal-side 4b8faded revision both define FG_ABI_MAX_BOS=1024 and FG_ABI_MAX_WIRED_BYTES=768 MiB; their public NativeAdrenoABI.h is identical. The kernel revision uses FG_ABI_CONTIG_TRY_BYTES=1 MiB and makes that physical-contiguous attempt inside its allocation path, while the Metal revision's corresponding kernel source uses 128 KiB and failure backoff, and moves portions of allocation outside the lock. These are source-level differences only: they do not establish the reason for the user's already-observed CPU-only lag or quantify this particular device's GPU performance.

Fetched mesa-fuxi-bo-cache.patch from bangprovn/ios-fuxi f44bf5693da18ac11abfa510c5ad711fc316254b, verified Git blob SHA-1 ebb5298bdb191062d75a0b995ff32c9cede745fe and recorded SHA-256 224b03a1a9e7e7ac268424a668dcccab69e36b56247a34547b54990ff1fe1150. The patch retains completed BOs with kernel handles in an uncapped cache; source includes memory-pressure and age trimming plus a cache flush and retry on allocation failure. The resulting resource behavior against this exact older kernel and iOS compositor is unverified. TU_DARWIN_BO_CACHE=0 is an indicated source-level comparison setting, but applying it and checking actual process environment requires separate boot/runtime validation. Code inspection cannot establish a safe working combination, and the identified differences do not replace Mac tests or real-device evidence.

## Native macOS frontend CI — 2026-11-08

With user approval, created public repository scyx2611/fuxi-metal-frontend-check-20261108 and ran workflow commit 9b7045dffaa34df859f5b8b49d1416a932827c67, GitHub Actions run 42479838583. The macos-15 job fetched public source revision 4b8faded6e76a4730832144fc3cae34c4419fa51 and completed successfully with Xcode 16.4. Full log: mac-frontend-run-42479838583.log. It includes the native gated-init test, native frontend and model suites. Test-injected error and unsupported-selector messages occur in successful negative cases; inspect individual test coverage before drawing conclusions. This validates the host suite at that revision, not iOS execution, the mixed-version kernel/Metal/Mesa bundle, actual GPU submission, screen composition, or recovery behavior on the phone.

Current Recovery read-only inspection confirms serial <序號>, root access, CPU candidate inode 152183 and GPU candidate inode 157885, both 20 GiB; /data has 106 GiB available. GPU candidate currently reports pin flag 0x800000; a full fresh extent and mapping safety audit is still necessary immediately before writes. The host GPU root image SHA-256 was freshly verified as 2f12014a3f8f6a6200b094ee13b73c6615498c644276caea936b1c7aa02e227. No image was written or booted during this continuation. Current source xnu/fuxi-unixnu/sweet_ufs.c describes fuxi-root-extents in userdata partition blocks and uses its own UFS I/O; source inspection does not prove behavior of the separately built kernel binary or render default-key mapping safe.

Fresh read-only mapping follow-up: fiemap-snapshot-after-ci.json is byte-for-byte equal in its item data to fiemap-snapshot-continued.json: all four pinned files cover their sizes, within the script's checked partition bound, and are mutually non-overlapping. The GPU candidate has 46 extents. Read-only inspection of the actual pinned kernel revision's 0088-fuxi-extent-root.patch (saved as abi-compare/kernel-979e-0088-fuxi-extent-root.patch) shows a 512-extent source limit and a userdata-relative root map; artifact-to-source provenance and live operation remain separate questions. Recovery mountinfo shows /data and /sdcard use F2FS on dev 254:8 (dm-8), whose sole visible slave is sda32; sda32's visible holder is dm-8 and both report 240597577728 bytes. A best-effort scan of visible process mount namespaces found 325 references to mnt:[4026531841]; one process disappeared during the scan, so this is not an exhaustive unmount proof. No write or unmount was performed. These checks are transient and must be repeated at the write boundary.

## AOSP libfiemap investigation — 2026-11-08 continued

Device <序號> was verified in Recovery. The F2FS mount instance /sys/fs/f2fs/dm-8/features contains pin_file; the generic /sys/fs/f2fs/features/pin_file alone would not prove the mounted filesystem uses it. A new read-only ioctl helper check-pin-readonly.c queried FS_IOC_GETFLAGS and F2FS_IOC_GET_PIN_FILE: both GPU /data/ios16-gpu-c.img and preserved CPU /data/ios16-codex-b.img had flags 0x800000 and moved-blocks count 0. These are point-in-time checks, not proof about future mapping stability or safe raw writes. The helper was pushed only to /tmp; no candidate or partition write was performed.

Reviewed Android 13 AOSP system/core fs_mgr/libfiemap sources, retrieved under aosp-libfiemap. FiemapWriter verifies both pin flag and moved-block count, and checks the mounted F2FS pin_file feature for reliable pin-before-allocation support. ImageManager creates and tracks image metadata and maps appropriate raw backing extents using device-mapper; its ZeroFillNewImage explicitly explains that file writes through metadata-encrypted userdata and raw mapped blocks differ. An AOSP managed image has additional metadata and mapped-device state; a manually pinned file is not automatically an ImageManager-managed image. The source implementation gives a candidate approach but does not prove it is safe to adapt or use with this candidate, Recovery environment, the Fuxi boot chain, or later Android access.

Linux FIEMAP documentation states that in-place updates via locations returned by FIEMAP without filesystem assistance are undefined and accessing those locations while the filesystem is mounted is not generally supported. AOSP has a specialized mechanism for managed backing images; do not equate its invariants to general FIEMAP use or assume an unmount cures the raw-write problem. The prior guarded writer is not authorized merely by its mount check; retain the no-write decision until an independently supported end-to-end path is established. This investigation did not write or boot a GPU image. The GPU-composited home screen and device behavior remain unverified.

Sources: https://android.googlesource.com/platform/system/core/+/refs/heads/android13-release/fs_mgr/libfiemap/ and https://docs.kernel.org/filesystems/fiemap.html

## Android service and root check — 2026-11-08 later

ADB device <序號> was confirmed running Android (sys.boot_completed=1); shell UID 2000 and build type user. KernelSU manager package me.weishu.kernelsu v3.2.4 is installed; no accessible su binary or /sys/module/kernelsu/version was found by these checks. Installation does not establish active KernelSU or shell authorization. Device lockscreen was showing during inspection; no unlock or security change was attempted.

Rebuilt the read-only GsiInspector to use ServiceManager.waitForService. It connected to gsiservice and reported isGsiInstalled=false, isGsiRunning=false, isGsiInstallInProgress=false. openImageService("ota") returned SecurityException: UID 2000 is not allowed. No managed image was created or modified, no partition was written, and no GPU image was booted. Existing extent snapshots remain invalid after Android reboot.

## Managed-image design constraints — 2026-11-08 later

The locally retained Android 13 AOSP source is a design reference only, not proof of the installed Android 17 implementation. Its SplitFiemap::Create removes existing split files at the selected header path before creating new files, so a future experiment needs a freshly generated unique name and explicit collision checks across both backing files and metadata; assuming create is non-destructive is unsafe. DeleteBackingImage refuses deletion when mapped, and the ramdisk/recovery path refuses deletion of images backed by /data. Thus any recovery experiment must have a verified lifecycle and cleanup route, not assume recovery can delete it.

SplitFiemap can split one managed image into numbered pieces. The existing build-gpu-c-payloads.py expects one 20 GiB file and a single continuous logical FIEMAP sequence. If the managed image has multiple pieces, its current output cannot be used unchanged: piece-relative logical offsets must be combined in actual managed-image order, reconciled with official mapped metadata and raw device-relative extents, and validated against coverage, overlap, bounds and the kernel's 512-extent limit. The 20 GiB image must then be fully read back through the same managed mapping and revalidated for persistence before any loader uses it. No managed image or new mapping has been created.

## KernelSU module check — 2026-11-08 later

/proc/modules on the currently booted Android reports a loaded module named kernelsu (196608 bytes). This corrects the earlier uncertainty about module presence, but does not prove the manager recognizes it or that UID 2000 has root access. Shell attempts to execute su and /system/bin/su returned no such file (exit 127); adbd remains a non-root production daemon. An ADB request started the installed KernelSU MainActivity while the lockscreen remained visible. Await an unlocked device and the manager's displayed state or shell authorization before using root; no privilege bypass, boot/security changes, image writes, or GPU boot occurred.

## KernelSU manager confirmation — 2026-11-08

User-provided screenshot shows KernelSU v3.2.4 reporting Working (LKM), version 32457; manager reports one authorization, without identifying the authorized app. Phone subsequently appeared unlocked and manager UI was launched by ADB. ADB input tap was rejected with SecurityException: INJECT_EVENTS, so automated navigation is unavailable under current privileges. ADB shell remains UID 2000 with su absent, consistent with no proven Shell grant. User action is needed to inspect and enable Superuser for the exact com.android.shell entry in KernelSU, then verify via a new ADB shell. Granting it is a privileged access change; avoid toggling any other application or updating KernelSU. No managed image or partition has been written.

## Android managed-image probe — 2026-11-08

User screenshot showed Shell (com.android.shell) marked ROOT; fresh ADB `su -c id` confirmed UID 0 in u:r:ksu:s0. The root GsiInspector could open ota image service and read an empty image list. A unique isolated pair of directories /data/gsi/codexgpu1108a and /metadata/gsi/codexgpu1108a was created with gsi_data_file / gsi_metadata_file labels. A disposable 16 MiB managed image codexgpu_probe_1108a was created with CREATE_IMAGE_ZERO_FILL, mapped by the official service, written with a test pattern, fully read back, unmapped and remapped. Hash before/after remap matched source: 437b15fd50a0951c8dc5cf1aad29ebe2af6a33c7f0302b7f54f1ac28a2d89fef. The ordinary backing file view had a different hash, consistent with metadata encryption.

At this moment the mapping was /dev/block/dm-8; its name must be rechecked every use. Read-only DM_TABLE_STATUS reported one linear target, 32768 sectors, backing 259:16 (sda32), offset 254132224 sectors, exactly matching the one 16 MiB FIEMAP extent at byte 130115698688. Backing inode 165553, pin flag 0x800000, moved count 0. This proves the single small current mapping only, not the 20 GiB candidate, kernel execution, cross-reboot persistence, full image safety, or GPU behavior. Probe remains mapped and its temporary host/phone pattern files remain. No pre-existing candidate or Android partition image has been overwritten. All data before and after reboot must be reacquired; never reuse dm numbers or stale extent snapshots.

After Android reboot the device booted normally, `su` remained available, and the managed image persisted unmapped with the same inode, pinned flag and moved count 0. The official service remapped it; full 16 MiB readback hash still matched 437b15fd50a0951c8dc5cf1aad29ebe2af6a33c7f0302b7f54f1ac28a2d89fef and DM table still matched FIEMAP at that moment. Official unmap and delete then succeeded; getAllBackingImages returned empty and both dedicated directories were empty. The dedicated namespace remains for future managed candidates. One 16 MiB test does not prove 20 GiB allocation, every extent, device kernel compatibility or GPU execution. Android lockscreen was present after reboot.

## Full managed GPU image — 2026-11-08

Created separate official managed image codexgpu_root_1108a under the dedicated codexgpu1108a namespace. It consists of a single 20 GiB .img.0000 file, inode 168843, 21 extents. Full fresh checks matched every DM_TABLE_STATUS linear target with FIEMAP, verified pin flag and zero moved count, coverage, partition bounds, and disjointness from the four existing candidates. The initial attempted Windows ADB stdin stream stopped after 1337 bytes in the new candidate: a 1 MiB binary test reproduced truncation even when writing to a disposable ordinary file. No pre-existing candidate was touched. Replaced that transport with adb push to /data/local/tmp/codex-gpu-stage-1108a.img; source on phone matched SHA-256 2f12014a3fb8f6a6200b094ee13b73c6615498c644276caea936b1c7aa02e227. A guarded dd wrote all 21474836480 bytes via the official mapped device. After official unmap/remap, entire mapped device readback SHA-256 matched the same value; fresh map and pin checks passed.

Using the then-current validated snapshot, generated CPU fallback, GPU-driver, and GPU-home payload/boot candidates in *-managed-gpu-loader directories. Existing CPU loader relink/mkbootimg was reproduced byte-for-byte first. Their ADT root map is point-in-time; recheck immediately before boot and rebuild if changed. No new candidate has yet been booted. GPU home, GPU composition, compatibility of the mixed-revision artifacts, and CPU fallback on this managed root remain unverified. The user must be present to observe the real phone screen; test CPU fallback first.

## First managed-root CPU boot — 2026-11-08

After full device readback verification, created matching CPU, driver and GPU-home boot images. An official unmap followed by fastboot boot of the CPU fallback candidate entered the XNU diagnostic monitor. Logs confirmed "FUXI UFS: root file in userdata: 21 extents, 5242880 blocks", HFS mounted as / and SpringBoard exec. The first diagnostic USB session disconnected and the phone subsequently returned to Android; the exact cause was not established (default loader reset deadline is a possibility). User reported a loading animation, not a confirmed interactive home screen.

On the next Android boot, checked managed image still exists and pinned, moved count 0; official map's 21 targets matched fresh FIEMAP and all existing candidates remained disjoint. The payload maps still matched; unmounted the managed mapping and fastboot booted the CPU fallback a second time, with monitor reset deadline extended to 10 minutes. This boot again mounted the root and executed SpringBoard. After 95 seconds the monitor captured framebuffer.png showing an iOS-style lock screen. This is buffer evidence only: actual panel and swipe/unlock must be confirmed by the user. No GPU home boot or GPU composition verification has occurred. Logs: cpu-managed-boot-log and cpu-managed-boot-log-2. The current device is in the experimental iOS CPU boot; eventual reset deadline is finite. Reacquire and validate extents after any Android reboot before reuse.

User observed the lock screen on the physical phone but touch did not respond. cpu-managed-boot-log-2 shows SweetHIDTouch registered/opened but FUXI TOUCH reset event 00 00 00 00, chip id 0000, then stopped. The monitor was run without --touch-power; docs-lanes/touch-display.md explicitly describes the working runner keeping --touch-power before XNU handoff. This is a concrete candidate cause, not yet a verified fix. CPU interaction and GPU qualification remain unpassed. Plan a controlled reboot, reacquire the managed map, then retest the same CPU boot with --touch-power and check actual chip ID and user swipe.

Retested CPU fallback after a controlled reset and fresh Android mapping/pin/disjointness/payload-map checks. This time used the monitor's --touch-power before XNU handoff, --reset-minutes 10. Startup logs now show reset event 03, chip id 3640 and "sensing, polling GPIO25"; the earlier attempt without touch-power showed zero chip ID and stopped. Await user confirmation of physical swipe and home screen. Successful initialization alone does not prove touch interaction or GPU rendering. Current CPU monitor logs are in cpu-managed-touch-boot and cpu-managed-touch-followup.

User confirmed physical touch works after --touch-power and asked about GPU. This boot has no fuxigpu/fuxibbgpu flags, so GPU composition was not expected. Await explicit home-screen confirmation and log evidence; next stage is GPU driver enabled but backboard GPU gate disabled, followed by opt-in GPU composition only if safe checks pass.

User confirmed that physical touch worked and swipe entered the home screen during the CPU fallback boot. cpu-managed-to-gpu-reset/boot.log captured actual SweetHID touch down/up and contact events. Android subsequently rebooted as scheduled; current candidate may have been modified by iOS, so preserve it and reacquire all mappings, rather than restore the initial image. Proceed to GPU-driver-only boot with touch power and verify GPU initialization and CPU home before attempting backboard GPU opt-in.

## GPU-driver-only device result — 2026-11-08

After CPU fallback screen and touch were physically confirmed, revalidated managed mappings and booted gpu-driver-managed-gpu-loader with --touch-power and 10-minute reset deadline. The GPU reached stage 16, but PAS init_image (SMC 0x42000201) returned x0=0xffcfffbb; zap=0xf07, clear/copy=0, and kernel explicitly logged "disabled permanently: bring-up/self-test (0x10), CPU present". SpringBoard and touch driver started, but GPU composition is not active. Do not proceed to the backboard GPU boot until initialization is resolved. User also observed no GPU acceleration.

The packaged loader uses D:/xiaomi13/fuxi_ios/firmware/a740_zap_fuxi.mbn, SHA-256 2015e9bb81b1f6efed11fdab0a5884b3f308ca41d50f4315e3ed2e36ee5e57f1. Its concatenated ELF header/hash equals local a740_zap.mdt, and its LOAD matches a740_zap.b01. docs-lanes/gpu.md reports an earlier stage-16 pass using a different 12088-byte zap with SHA-256 86e4c3f61d4ddbe581405f4cc496182cf385bee39aaefec61639a31d3619fd35. That distinction does not prove the failure's cause, nor that the other firmware matches the current device. A Linux kernel change records the same raw error on a different Qualcomm platform when its TZ shared-memory bridge is used (https://github.com/torvalds/linux/commit/8342009efa2a5e75dce56173d7de026bcc6666d8); this is an investigative clue, not proof of the same cause on this device. Compare active Android firmware and the exact PAS memory/metadata path before modifying boot artifacts.

## Active Android zap firmware finding

After returning to Android, dmesg showed ueventd loads `a740_zap.mbn` from `/odm/firmware/a740_zap.mbn`; it did not use the vendor split image that had been packed. The actual active ODM MBN is 12088 bytes, SHA-256 cad0dcb2daaa382eebc94ef65f3285b91b045c8976781f6a80910ca21c933a05. It was copied read-only from the running phone, and the host copy matched that hash. ELF structure, three program headers and code segment layout match the previous image; LOAD segment contents are identical, but metadata/signature differs. Android logs establish selection, not successful GPU initialization in the iOS environment.

Built independent gpu-driver-odm-zap-loader and gpu-home-odm-zap-loader by substituting only the exact device's ODM MBN while retaining the same GMU/SQE and payload. Verified existing old gpu-fw.o matched its three original inputs, checked new input hashes, validated the embedded full firmware section, loader structure and boot images. No security check was bypassed. Driver-only test is required before opting in backboard GPU, and all managed image extents must be reacquired after each Android boot. The matching ODM variant has not yet been booted.

## Active ODM zap driver boot — 2026-11-08

Booted gpu-driver-odm-zap-loader with --touch-power and a 10-minute reset deadline after fresh map/pin/disjointness checks. Secure firmware calls init_image, mem_setup and auth_and_reset all returned 0; stage 16 PASS reported fence=0xa740f00d, TRUST=0, zap=1 and pixels=1; kernel reported GPU ready and subsequently emitted GPU copy-source logs. Compared with the vendor split firmware boot that failed PAS init_image, use of the Android-selected ODM MBN resolved this device's observed initialization failure. User confirmed physical touch works but the UI remains sluggish. GPU initialization/self-test and GPU copy logs do not prove the backboard/Metal compositor uses GPU; the boot intentionally omitted fuxibbgpu=1. Logs: gpu-driver-odm-zap-boot/boot.log. Next controlled test is backboard opt-in with the same ODM firmware, fresh extents, live logs, and physical screen observation.

## GPU home test exposed missing ABI switch

Booted gpu-home-odm-zap-loader with fuxigpu=1 and fuxibbgpu=1; GPU stage 16 and pixel self-test again passed and touch initialized, but boot.log repeatedly showed IOAcceleratorES matching zero services and no FUXI MTL/NativeAdrenoGpuPublished lines. Inspection of the exact 979e source and docs-lanes/metal.md confirms NativeAdrenoGpuPublished is gated separately by fuxigpuabi=1. Our generated GPU payload command lines omitted that switch; the driver-only and attempted home boots therefore could not expose Metal, independent of their passing hardware initialization. This is an implementation error in our candidate generation, not evidence that GPU composition works. Build new variants with fuxigpuabi=1, first verify published service and normal CPU fallback with fuxibbgpu absent, then test the GPU home opt-in. Preserve existing candidates and repeat mapping checks.

The revised driver-only ABI candidate (fuxigpu=1 fuxigpuabi=1, no fuxibbgpu) passed stage 16 and published NativeAdrenoGpuPublished v2 HostImport. IOAcceleratorES matching found one service; actual backboardd and SpringBoard emitted FUXI MTL gate-deny lines as expected without the opt-in argument. User reported touch was initially unresponsive but later recovered; logs captured actual touch contacts, and user reported UI remained slow. This validates discovery and CPU policy fallback in this boot, not GPU composition. Next test the separately built GPU home ABI variant. Logs: gpu-driver-abi-boot/boot.log.

## GPU composition attempt and fallback — 2026-11-08

Fresh boot with active ODM zap and both fuxigpuabi=1/fuxibbgpu=1 passed GPU stage 16, published the service and admitted backboardd (armed, uid=501). It opened the GPU ABI, created backend state and reached a concrete unsupported texture: `texture reject pixelFormat 550 desc=type=2 format=550 size=776x24x1 ... usage=0x5 ...`. The gate recorded runtime-failure persisted=1, exited for CPU respawn, and the next backboardd logged deny reason=strikes strikes=1. No first frame or good frames, and no increasing GPU composition frames, was established. User reported severe lag and could not get past the physical lock screen. Device qualification is stopped; restore the known CPU fallback. The 550 format is deliberately refused by PixelFormats.hpp and docs-lanes/metal-m9n.md identifies it as private two-plane RGB10A8_2P_XR10 with unproven layout and conversion. It must not be aliased to UNORM or accepted without correct XR render/sampling/clear/blend/packing semantics. Actual evidence in gpu-home-abi-first/boot.log.

## NativeMetal alternative preparation — 2026-11-08

Following the failed GPU home attempt, restored the prior CPU fallback with fresh map/pin checks and --touch-power. User confirmed the physical phone could again enter the home screen but remained slow. The user agreed to use the public GitHub macOS test environment. `docs-lanes/metal-m9n.md` explicitly treats format 550 as private two-plane XR and disallows substituting UNORM. The pinned NativeMetal frontend/runtime is an alternative worth qualifying: its source delegates texture operations to a runtime that models two-plane formats, but Fuxi iOS 16 integration and actual device behavior remain unverified.

Added only a public-source workflow to the previously authorized public CI repository scyx2611/fuxi-metal-frontend-check-20261108, commit 6a5e80daa3884eb6fac580767ef9070cc361a21e, using the same public iOS-fuxi source revision 4b8faded6e76a4730832144fc3cae34c4419fa51. GitHub Actions run 42490973728 builds/audits nativemetal on macos-15 / Xcode 16.4, with full checks requested and no phone files or private data uploaded. The run has just been queued; inspect its live status and output before trusting or deploying anything. Passing a build will not prove correct or supported format-550 rendering, iOS 16 compatibility, or safe GPU home operation.

## NativeMetal patch-stack build repair

Both initial macOS NativeMetal builds failed with `Patched source mismatch`; reproduced it in an isolated LF checkout at the exact public pins. The final metal2vulkan patch stack adds `crates/metal2vulkan-runtime/src/tests/refuse_rebinds.rs`; the expected cached diff includes it, but ordinary `git diff HEAD` omits the newly created untracked file. The checker therefore rejected its own correct patch result. Implemented an isolated repair in ios-native-lf/tools/build-fuxi-nativemetal.py: reconstruct expected stack in a temporary index, permit only patch-declared additions, use intent-to-add in a copy of the real index to compare worktree bytes exactly, reject all unexpected untracked/staged/tampered files, and leave the real index unchanged. The actual pinned stack now prepares twice successfully; independent temporary-repo tests pass for a new file, idempotence, untouched real index, tampered tracked/new files, unexpected untracked files and staged changes.

Published only the patch, fixture test and public-source CI workflows to the previously authorized public repo, commit e0bd223. Mac iOS build run 42492105441 and Mac host integration run 42492105016 were started; both must complete and their logs/results must be examined. No private phone or image contents were published. This source-check fix does not itself add XR support or prove NativeMetal works.

## NativeMetal device smoke — 2026-11-09

The 20 GiB managed native candidate was completely read back after unmap/remap on the prior turn and matched source SHA-256 61425df281f368e845d2f64db775b073813db665e029e9c753b36edccc365774. FIEMAP, pinned state, all 39 managed DM targets and candidate disjointness were verified. A competing earlier monitor intended as a dry run actually performed XNU handoff on 2026-11-08; its progress.jsonl reached stage 5, with no smoke output captured. The later mapped image SHA-256 was 49e958e1043b432c7c30c49ac645357f0e48f9a7d8c2dbad5e2571a913fd6644; preserve that modified image rather than assuming the initial digest still applies. The unsafe convenience runner run-native-smoke.py is disabled pending a supervised and freshly revalidated run.

On 2026-11-09 revalidated pin, extents, all DM targets, disjointness and both embedded payload maps, unmapped officially, then temporarily booted native-metal-smoke-loader. The sole monitor used --skip-clocks --touch-power --reset-minutes 10; touch controller identified 3640. native-metal-smoke-boot-2/boot.log shows GPU stage 16 passed, NativeAdrenoGpuPublished registered and NativeMetal selected. The smoke process entered, Turnip submitted one IB, and then stage 0 failed the class/size/registryID guard before any smoke pixel assertion. It also logged six native capability-answer refusals: gpuCoreCount, maxVertexAmplificationCount, maxComputeThreadgroupMemory, isLargeMRTSupported, supportsComputeCompressedTextureWrite and supportsConditionalLoadStore. No GPU UI or usable rendering is demonstrated. The six refusals invoke the existing failure hook; they must not be suppressed or guessed merely to pass a compositor gate.

Source investigation: the smoke guard accepts only FuxiMTLDevice, but the selected native path returns a dynamically allocated NativeBridgeMetalDevice, itself derived from the guarded 528-byte _MTLDevice, with registry ID supplied by the wrapper. Prepared a narrow smoke-only class guard adjustment in ios-native-lf/tools/fuxi-metal-smoke.m and mac-ci-work/patches/native-smoke-class.patch; it retains the superclass, size and nonzero registry checks. The public CI workflow change is local and uncommitted; reverse patch applicability and whitespace were checked, but the modified executable has not been built, signed, staged or tested on device. Prior run 42492105441 was verified successful at e0bd223, which predates this adjustment. After monitoring ended, the probe reset deadline was reduced to one minute to return the device to Android. Reacquire maps after any reboot.

Further source inspection shows these six messages arise during installAnswers enumeration, where the runtime intentionally returns no value for unsupported or unmeasured properties; NBLOG calls the failure hook even before an external client queries them. For backboardd that hook faults the safety gate. Whether an absent optional answer can safely be omitted without faulting needs a targeted change and tests that prove any later actual request still fails closed; no values should be invented. Android boot_completed=1 was observed after the test and the native managed image remained unmapped.

User explicitly authorized committing the previously prepared smoke-class fix and running public macOS CI. Pushed harness commit 8be95c8de10883083ae3e30c516b57932cb7a192; run 42499459984 is the corresponding build. It contains the smoke-class patch and CI workflow only. A separate local candidate for optional-answer handling and a proposed Mac test live under native-optional-patch; neither has been committed, built or deployed. Its intent is to distinguish exactly the bridge's explicit no-answer result from other failures while retaining the existing forwarding failure hook on actual use. A fresh host smoke2 overlay has native-smoke-root.img (verified SHA-256 61425df281f368e845d2f64db775b073813db665e029e9c753b36edccc365774) as read-only backing. The currently modified phone candidate is being backed up to native-phone-presmoke2-backup.img through the verified managed mapping; do not treat the backup as complete until the running backup process exits successfully and hashes match.

The user paused and later continued. The first backup was interrupted at 3162505216 bytes and is incomplete; keep it distinct. CI 42499459984 completed successfully, smoke executable SHA-256 d5b0f19eba6c8011c7be146ca6fc78816031ea6e69a6c72dbd9b2ce37de4131c was checked against the downloaded manifest, and the build log identifies iOS 16 arm64 signed output. The executable alone was installed in the isolated smoke2 overlay, with HFS+ checks passing before and after; qemu-img compare says the converted 20 GiB native-smoke2-root.img matches the overlay, SHA-256 c8572430009de7a44faf49775b252a3f0ea13e7e1827ba8decd56a2f8a24c844. After the phone booted Android, the candidate was freshly remapped and FIEMAP/pinning/DM/disjointness verified. Backup retry is currently running as a gzip-compressed ADB stream, decoded to native-phone-presmoke2-backup-retry1.img. Its full size and SHA-256 must be verified before using it as a recovery copy or writing the new root. The prepared native-smoke2-write.sh verifies exact target name, size, source hash and a caller-supplied backup hash before writing; it has not been executed.

Backup retry completed successfully with exactly 21474836480 bytes. The streaming source and an independent host file read both produced SHA-256 c9cd76e67cee07349cee5ac5e5910dc248f44d1fbd0bf3bb401d0c6e8eebb7da, matching the mapped device hash measured before the stream. The candidate map remained pinned, disjoint and matched the managed DM table after the backup. This is a byte-verified backup, not a tested restore.

The new 20 GiB smoke2 image was transferred to /data/local/tmp and matched host SHA-256 c8572430009de7a44faf49775b252a3f0ea13e7e1827ba8decd56a2f8a24c844. Fresh verify-native-map.py and payload-map checks passed before a guarded write to the official codexnative_root_1108b mapping; the writer first checked exact mapped device identity, size, unmounted state, source digest and current candidate digest against the verified backup. dd wrote all 21474836480 bytes. Official unmap/remap and full readback matched the smoke2 source hash; temporary phone staging file was removed, maps checked again and the mapping officially unmapped before fastboot boot. The candidate is modified by the subsequent XNU boot, so the original smoke2 hash is no longer evidence of its current bytes.

First boot of the corrected smoke binary, native-metal-smoke-boot-3/boot.log: GPU stage 16 passed and the plugin admitted the smoke process; ABI open/query/close succeeded, but stage 0 exceeded its existing 30-second wall bound before device discovery completed. No smoke pixel or compositor result was obtained. In this boot, plugin constructor reported ctor_us=64918515, gate_us=74015949, install_us=80469134 and then ABI preflight; it did not reach the Turnip initialization messages seen in the earlier boot. Whether this was first-boot load contention or a deeper hang is unknown. Do not relax pixel/frame requirements or infer functional GPU rendering. Consider a controlled retry against the same now-initialized candidate after returning to Android and freshly verifying its mapping.

The controlled second boot of the same candidate used freshly revalidated extents and succeeded at stage 0: the smoke reported Turnip Adreno 740, NativeBridgeMetalDevice, base/instance size 528 and registry ID 4294967597. GPU initialization passed and a command submission occurred. Stage 1 then failed `late completed handler count 1`; because `finish()` tests completion handlers before `verify()` checks buffer contents, this does not prove correct GPU pixels. Existing source `addCompletedHandler` only appends to an array that `finish` drains once, so a handler added after completion is never invoked. Local draft patches 0004 and 0005 plus NativePortTests.mm cover initial, reentrant, concurrent and post-completion handlers, and distinguish explicitly unavailable optional capabilities from actual query errors. Patch application against the already-patched NativeMetal source was checked; the changes have not yet been compiled or device-tested. The GPU home gate remains disabled. Public CI workflow changes and these new source patches are prepared locally pending explicit authorization to commit them.

After diagnostic collection, the device reset timer was shortened; Android boot_completed=1 and native image mapped=false were confirmed. The two workflow changes, two new public-source patches and expanded NativePortTests.mm are staged in mac-ci-work for review but have not been committed or pushed. Both workflow YAML files parse and git diff --cached --check passed. The user has been asked for explicit authorization to submit the new implementation patches and run macOS host tests and iOS build; the prior authorization applied to the smoke-discovery fix.

User explicitly authorized these two implementation patches and their tests. Public harness commit 4642ce52d54554661fe920610e8f5199c491fc33 was pushed; iOS build run 42544946609 and Mac host-test run 42544946944 were dispatched at that exact revision. Check both results and logs before deployment. The isolated smoke3 staging overlay uses the independently rehashed smoke2 host image as its backing; the phone remains in Android and no new candidate write has occurred for these patches.

Both runs completed successfully at 4642ce52d54554661fe920610e8f5199c491fc33. The Mac host log reports the actual NativeMetal frontend tests passing, including the added callback and optional-answer tests; this does not validate GPU work. The iOS build log reports arm64 iOS 16 artifacts signed and audited; all 19 downloaded manifest hashes matched. The artifacts were staged into an isolated smoke3 overlay with metal-smoke.conf set to 6 and backboardd disabled. HFS+ fsck passed, qemu-img compare verified the 20 GiB raw export matches that overlay, and the source SHA-256 is 0fb5047a2fff9c603c8f78130dd894aead162376a2e167aef346fb808c7442b6. A new read-only phone candidate backup is still running to native-phone-presmoke3-backup.img; verify its completion and independently hash it before considering any device write.

The smoke3 pre-write backup finished at exactly 21474836480 bytes, and its independent host readback hash dc5c210f5eb2ad8f5d9455635a7815789b7ba6a04b4804791daae6e73e176214 matches the mapped device hash measured before streaming. The pinned 39 extents, managed DM mapping and disjoint candidates were revalidated. Transfer of smoke3 root to /data/local/tmp/codex-native-smoke3-stage.img has begun; an in-progress transfer is not a valid source for a write. No smoke3 image has yet been written to the managed mapping.

The smoke3 transfer subsequently completed, the phone-side SHA-256 matched 0fb5047a2fff9c603c8f78130dd894aead162376a2e167aef346fb808c7442b6, and a fresh map/payload check preceded the guarded 20 GiB write. The writer verified the candidate's current hash against the smoke3 pre-write backup. Official unmap/remap plus complete readback matched the source hash; the temporary phone transfer was removed and mapping/payload checks repeated before temporary boot. In native-metal-smoke-boot-5, GPU stage 16 passed but the initial fresh-root boot hit the smoke's 30-second stage-0 discovery bound, before any pixel result, matching the earlier first-boot pattern. Once this terminal failure was captured and logs flushed, the monitor process was deliberately stopped and the probe reset deadline set to one minute. Recheck Android and all mappings before one controlled retry of the now-initialized candidate.

After Android reboot and a fresh managed-map/payload validation, the controlled smoke3 retry succeeded on the actual phone: native-metal-smoke-boot-6/boot.log shows NativeBridgeMetalDevice discovery, 65536-byte GPU fill, CPU-write/GPU-copy/readback, 16 reuse rounds, BGRA8 triangle pixels (8192 red, 57344 clear), IOSurface 65536-pixel composition, and the 65536-pixel multi-layer/source-over test all PASS, followed by `FUXI MTL: done PASS`. The six optional unanswered properties were reported without falsely faulting the gate. This proves those offscreen GPU operations and readbacks only; it does not prove desktop composition, 60 Hz, stability or XR format 550 correctness. The monitor was deliberately stopped after the logged PASS and the reset deadline set to one minute.

The next offscreen Core Animation smoke executable is still hard-coded to accept only FuxiMTLDevice, although the working native backend actually returns NativeBridgeMetalDevice. A matching test-only discovery guard adjustment is prepared in ios-native-lf/tools/fuxi-ca-smoke.m and mac-ci-work/patches/native-ca-smoke-class.patch, preserving the base-size, class and registry checks. It does not change the CA pixel or 60 Hz requirements. Run the public build and verify its signed artifact before staging it; no Core Animation smoke has yet run.

The test-only CA discovery adjustment was submitted as public harness commit 8d70628f7398658b0d38305c3dca5a29865b8886, and iOS build run 42548657531 was dispatched. After the GPU PASS, Android boot_completed=1 was verified and the native mapping was reacquired and checked; a full pre-CA backup to native-phone-preca-backup.img is in progress. Its completion, device hash and independent host hash must all be verified before using it as a recovery point or overwriting the candidate. The host CA staging overlay uses the clean smoke3 source image and will only change the CA diagnostic executable; it has not yet been staged or written to the phone.

Run 42548657531 completed successfully at 8d70628f7398658b0d38305c3dca5a29865b8886; its manifest verified in full, and its CA smoke binary is iOS 16 arm64 signed, SHA-256 5ca913edda1ce466dcef9de49c1b209784bae8e82802eabd2db9e084136fe1e8. Only this executable was staged into the separate CA overlay; fsck passed before and after, the raw conversion compared identical with the overlay, and native-ca-root.img SHA-256 is 70e9eeeda72015a493d716a72f440ecda9c98b0c710ab21fe0d9c4fb7143a6de. The full pre-CA backup completed at 21474836480 bytes; phone source and independent host SHA-256 matched 8fab9ebad82f7c7e3d2446328260536d67fd82c1347c341f4b24ef0809c47f43. A transfer to /data/local/tmp/codex-native-ca-stage.img has started; verify its completion and hash before any guarded write.

The CA candidate was verified after transfer, written through the officially mapped candidate after exact source/current-backup hash and map checks, then unmapped/remapped and fully read back with matching SHA-256 70e9eeeda72015a493d716a72f440ecda9c98b0c710ab21fe0d9c4fb7143a6de. The first cold boot hit the established 30-second startup timeout; after returning to Android and fresh map/payload checks, the second boot native-ca-smoke-boot-2 reached CARenderer stage 0 and failed with `constant type 51 is not qualified`, SIGABRT. Type 51 is MTLDataTypeUChar3; the existing M9a observer notes that a single value carries three component bytes, with four-byte stride for ranges. NativeBridgeMetalFrontend's constantBytes explicitly omitted three-wide values. A narrow local patch for type 51 plus Mac test of the actual MTLFunctionConstantValues accessor is prepared in mac-ci-work; it is not yet built or deployed. Do not infer correct CA or screen rendering from the prior GPU-only smoke. The user reports desktop still laggy, consistent with the CPU fallback.

The monitor disconnected after the CA failure while the probe still reported stage 8; a one-minute reset deadline was requested and Android boot_completed=1 subsequently confirmed. C: had approximately 3 GB free and D: nearly none, so new task artifacts will use the newly created E:/Codex-Xiaomi13-GPU-20261109 directory. A fresh pinned/mapping/disjointness check passed and the current managed candidate backup to that directory is in progress; verify exit, full byte count and independently hashed contents before using it as a recovery point. The user was asked for explicit authorization to commit and build the new uchar3 patch; no approval had yet been received at the time of this entry.

The E: pre-uchar3 backup completed at 21474836480 bytes. The initial mapped-device digest, streamed data digest and independent host file readback all matched SHA-256 746f5905e70d180c70cee18dbf79c1bfcc7105f7932acaa3ef5abb635e86e5bd. A fresh managed-map verification passed after backup, and the mapping was officially unmapped. The prepared E: qcow2 overlay references the clean native-ca-root.img whose SHA-256 was independently reconfirmed as 70e9eeeda72015a493d716a72f440ecda9c98b0c710ab21fe0d9c4fb7143a6de. The uchar3 patch and expanded host test remain local and unbuilt, pending user authorization for public Git submission and CI.

User authorized the uchar3 patch and tests. Public harness commit f6b9872b162fd5d53a687eb2af47da62eae2d033 was pushed; iOS build 42554473681 and Mac host test 42554473411 were started at that exact commit. The candidate accepts only MTLDataTypeUChar3 (51) with its three component bytes; other unsupported three-wide types remain refused. Host test checks actual MTLFunctionConstantValues named-value ABI and all three bytes, in addition to previous safety and completion controls. Both jobs must pass and signed artifacts and manifest be checked before deployment. The phone remains in Android; E:/Codex-Xiaomi13-GPU-20261109 holds the verified pre-uchar3 backup and an isolated staging overlay, with no new write to the phone.

Both uchar3 CI runs succeeded at f6b9872b162fd5d53a687eb2af47da62eae2d033. The Mac host log records the actual frontend test PASS, including the uchar3 constant accessor and existing failure/handler checks. iOS arm64 16.0 artifacts are signed and audited; all downloaded SHA256SUMS entries matched. They were staged into E:/Codex-Xiaomi13-GPU-20261109/native-uchar3-root.qcow2 with smoke stage 6, CA stage 5, and no backboardd allow. HFS+ checks passed and qemu-img compare verified the 20 GiB raw export matches the overlay; native-uchar3-root.img SHA-256 ecc629a0f183215f82b303adca80dc17ae56a5086624f0a7a892552b77a1aec4. No device write of this candidate has yet occurred. Before any guarded write reacquire/verify the mapping and check its current full hash against the verified pre-uchar3 backup 746f5905e70d180c70cee18dbf79c1bfcc7105f7932acaa3ef5abb635e86e5bd.

The uchar3 image was subsequently transferred and the full phone staging hash matched ecc629a0f183215f82b303adca80dc17ae56a5086624f0a7a892552b77a1aec4. After a fresh map/payload check, a guarded 20 GiB write checked the current device against the verified pre-uchar3 backup before writing. Official unmap/remap and full readback matched the source hash; temporary phone staging was removed. Fresh map/payload checks passed again before CA smoke boot. First boot E:/Codex-Xiaomi13-GPU-20261109/native-ca-uchar3-boot-1/boot.log shows GPU stage 16 passed, but CA device initialization hit its 30-second watchdog, consistent with previous cold-start behavior; no CA pixels were produced. After recording this failure, monitor was stopped and the probe reset deadline set to one minute. Reacquire and revalidate all maps after Android boots before controlled retry; no UI GPU qualification yet.

## Read-only host import investigation — 2026-11-09

Controlled retry E:/Codex-Xiaomi13-GPU-20261109/native-ca-uchar3-boot-2/boot.log passed CA solid, opacity and rounded clip pixel checks, then image stage 3 failed with `host import refused stage 4 ... detail 0x4`. The actual 979e2f user-client source defines bit 2 of this bitmap as success of a whole-range read-only memory descriptor, while the read/write descriptor and first/last read/write page probes failed. This supports a read-only protection mismatch; it does not alone prove why the mapping has that protection or guarantee a GPU fix. Core Animation desktop and frame rate remain unverified.

The matching source has an opt-in read-only import path on `fuxigpuncimport` bit 10 (1024). It wires a read-only kernel alias and removes GPU write permission from such imports; the existing synthetic HostImport model tests cover creation and cleanup, but not actual hardware enforcement, coherency, image correctness or the possibility that a client later attempts a write. Baseline logs had no `fuxigpuncimport` argument. For a bounded diagnostic, E:/Codex-Xiaomi13-GPU-20261109/prepare-roimport-loader.py prepared E:/Codex-Xiaomi13-GPU-20261109/native-ca-roimport-loader/boot.img (SHA-256 5d73882f57944b081989a1fccaa29f2af2219a50a839d94323f0511a09abc6bb). Its payload differs from the previously used CA payload only in the boot-argument field, adding `fuxigpuncimport=1024`; firmware hashes, image structure and embedded payload were checked. The loader is not yet booted or qualified; it carries the previous candidate extent snapshot, so reacquire and validate all maps and the embedded extents after Android boot before use. Never infer that GPU writes to a read-only import are supported. Preserve CPU fallback and stop on fault or bad pixels.

At investigation time the previous 360-second monitor was still running against the active Fuxi hardware probe (USB VID 1209 PID 0001); ADB and fastboot do not list the phone in this mode. Allow the monitor to finish, then restore Android with the known reset mechanism and verify state before any new temporary boot. The phone has not been written or rebooted in this investigation.

Follow-up: Android returned with boot_completed=1, root, and serial <序號>. The native image was mapped through its official service, verify-native-map.py passed (39 extents, exact DM/FIEMAP, pin and candidate disjointness), and verify-native-payload-maps.py confirmed the original payload map; the new payload was byte-compared outside boot args to that original. The image was officially unmapped before fastboot boot of the diagnostic loader. In E:/Codex-Xiaomi13-GPU-20261109/native-ca-roimport-boot-1/boot.log, CA stages 0-4 passed, including the previously failing image stage and transformed image. The 1080x2400 stage completed its pixel acceptance and 60 GPU-rendered frames but failed its timing gate: 47/59 warm frames missed the 60 Hz deadline. Warm render wall time min/median/max was 4.864/6.002/11.252 ms; pacing sleep overshoot was 4.925/27.538/100.032 ms. An independent 4 ms sleep probe had idle median overshoot 100.031 ms and busy median 49.604 ms; accepted time-constraint/QoS calls left displayed priority 4. These observations indicate a scheduling/timer issue worth investigating, not proof of its root cause or real desktop performance. Stage 5 correctly reports FAIL, not PASS; do not disable the gate or enable desktop composition solely on these results. The GPU statistic of 3.258 ms median must be interpreted in the context of the native frontend's implementation, not as independent hardware timestamp provenance. Monitor was stopped after the terminal failure and a one-minute probe reset requested; confirm Android before further action. No phone storage write occurred in this follow-up.

Android boot_completed=1 was subsequently confirmed. An isolated qcow2 overlay over independently rehashed native-uchar3-root.img was mounted in Alpine QEMU, and its actual launchd cache had exactly one peakpowermanagerd job with ProcessType Adaptive. E:/Codex-Xiaomi13-GPU-20261109/native-priority-share/priority.py changed just that job to Interactive, verified decoded plist equivalence for every other key and verified reread; fsck.hfsplus passed before and after. The raw export compared identical to the overlay and has SHA-256 c0859664be6d2750e1872faac84f21a19f842591199a1a9f597ddffeb2bb59fe. This is a diagnostic scheduling control, not yet a desktop fix. A new 20 GiB backup from the mapped current candidate to native-phone-prepriority-backup.img and an independent transfer of the new image to /data/local/tmp/codex-native-priority-stage.img were started concurrently. Both must complete and their hashes be verified, including independent host readback of the backup; revalidate map, pin, disjointness and mapped candidate full current hash immediately before any guarded write. Never use a partial transfer or backup. No candidate write has been performed as of this entry.

The priority staging transfer completed and phone SHA-256 matched c0859664be6d2750e1872faac84f21a19f842591199a1a9f597ddffeb2bb59fe. Backup remains in progress. In parallel prepared a separate survey-only root with the normal Adaptive launch job and `--enable-backboardd` via the existing stager; client allowlist and HFS+ checks passed, the raw export compared identical with its overlay, SHA-256 344c68748b61c4b275d4fcdd41146316e01e6a5cd6200c15e895a56a9a81327f. The separate native-backboard-survey-loader uses fuximetal=native, fuxigpuncimport=1024, fuxibbgpu=2, no execswap; boot SHA-256 e88f139ee4ddbfcac5acf6ce8461f3fbf9abc6287622a21f183f4273011ba92a. It contains an old extent snapshot and must be verified afresh after any Android boot. The survey root transfer to /data/local/tmp/codex-native-survey-stage.img is in progress; neither survey root nor loader has been applied. Use only after the unchanged CA stage 5 passes, and preserve CPU recovery.

The prepriority backup completed at 21474836480 bytes; mapped source, transfer and independent host readback all matched SHA-256 b1f52b26c730995c4d70850c11c81aba64fb0b29bf3a4c5ff1466813773df930. A fresh map check passed; guarded write of priority root verified both source and current target digests, wrote all bytes, and after official unmap/remap full readback matched c0859664be6d2750e1872faac84f21a19f842591199a1a9f597ddffeb2bb59fe. Map and original payload checks passed again before unmap and temporary boot. native-ca-priority-boot-1/boot.log shows stage 0-4 pass; priority moved from 4 to 97 after pacing request, independent idle sleep overshoot median fell from 100.031 to 0.023 ms, and the 1080x2400 pixels passed with 0/59 late and warm wall 4.973/5.742/11.953 ms. Stage 5 nevertheless reported FAIL because the combined `failures() || hits()==startHits` gate was true and the native cache hit delta was 0 (misses 8). No native refusal was logged, but this combined check does not independently report failures(), so do not assert zero. Cold root had no cache hits; a controlled retry after Android and map revalidation can test whether persistent shader cache changes this, without weakening the gate. Monitor stopped and reset requested; confirm Android. The survey root staged transfer completed with phone digest matching 344c68748b61c4b275d4fcdd41146316e01e6a5cd6200c15e895a56a9a81327f; it remains unused.

After Android boot_completed=1 and fresh managed map/payload validation, the same priority candidate was temporarily booted again without a rewrite. native-ca-priority-boot-2/boot.log reports stages 0-4 PASS and stage 5 PASS: 2,592,000 pixels, 60 frames, 0/59 late, warm wall min/median/max 5.271/6.224/11.576 ms, 4 cache hits and 0 misses. The original gate also checked no frontend failures, positive draw/sample metrics and pixel acceptance; 2640 GPU draws were reported. This is offscreen CA, not physical desktop composition or an independent proof of 60 Hz panel output. The monitor was stopped and a one-minute reset requested; confirm Android. Before writing the previously prepared survey root, acquire and independently verify a full backup of the currently modified priority candidate and revalidate maps; do not use its original preboot digest. Survey is diagnostic and user is away from the phone, so physical screen and touch confirmation must remain unverified.

The full pre-survey backup completed, device, stream and independent host readback all matching SHA-256 1fcad55006ab3d2acf183d0eb6dbcb20a37e45748c4173ea46602c0fa61d66e7. The survey image was guarded-written after a fresh verified map and exact current/source digest check; official unmap/remap full readback matched 344c68748b61c4b275d4fcdd41146316e01e6a5cd6200c15e895a56a9a81327f. Fresh map and embedded survey payload controls were verified again before unmap and fastboot boot. E:/Codex-Xiaomi13-GPU-20261109/native-backboard-survey-boot-1/boot.log shows backboardd gate armed, native backend admitted, then `IOSurface refused step=3 fourcc=0x62336138 plane=0 planes=2 offset=0 row=0 bytes=13893632 size=1125x2436 pixel=550`. Seven refusals correspond to one distinct surface problem; survey exited after its 20-second window, persisted a strike and the next backboardd instance was denied GPU in that boot. No GPU-composited desktop frame was counted. SpringBoard later executed, but actual display and touch were not observed. Source check: step 3 is the plane base/range or zero stride check; in this observed message offset is zero and row is zero. No assumption about where the zero was introduced or the actual XR plane layout is yet justified. Monitor stopped and one-minute reset requested; ADB reappeared. Verify Android boot_completed and native mapping state before further work.

Read-only analysis of the exact iOS root's dyld cache in an isolated QEMU mount located `_IOSurfaceGetBytesPerRowOfPlane` at 0x1a398138c and its internal implementation at 0x1a3983418 (E:/Codex-Xiaomi13-GPU-20261109/surface-plane-inspect.log and surface-cache-inspect.log). It reads plane stride at IOSurfaceClient +0xe0 + plane*0x80. The same getters read width +0xd0, height +0xd4, offset +0xdc, element fields +0xe8/+0xea/+0xeb. The kernel result is copied at client +0x68; source at build commit 979e2f places those fields consistently except stride, which it only writes at plane +0x08 (client +0xd8), leaving expected plane +0x10/client +0xe0 zero. Exact pinned kernel source SweetSurface.cpp hash matches local copy. This is direct source/accessor evidence for the zero stride, not proof that XR composition/display is otherwise correct.

Isolated kernel-source-979 modifies the original 0046 patch and mirrored source to additionally write plane stride at +0x10, retaining the existing +0x08 value; regenerated 0108 patch shifts hunk positions only. In Alpine QEMU, the actual encoder test failed against the old code at the two-plane stride assertion and passed with the patch, including two different strides, width/height/offset/size/element fields, invalidity flag and nonplanar control. sync-fuxi-scanout.py --check passed; it emits an existing trailing-whitespace warning inside an unrelated context. No Apple image contents or device data were submitted. Public harness commit a6fafa62408807bc8730fd02c0125f5ee21ae768 adds the kernel patch, this test, and a Mac build workflow; GitHub run 42569395900 is in progress for pinned 979e2f with iig replay, and its patch/test step passed. Check the run, checksum manifest and exact-source build log before using any artifact. Preserve old kernel loader and CPU fallback. The user is away, so physical display verification remains unavailable.

Repacking the existing old 979e kernel with the pinned mkpayload.py, the survey payload's extracted private ADT and identical arguments produced a byte-for-byte identical survey payload (SHA-256 deffccee30f4dfb0496de6a63ec39cc15efc559cb9d9a53d558f8889eb05a624). pack-surface-fixed-payload.py and prepare-surface-fixed-loader.py are prepared locally for the new artifact, with map and argument assertions; neither has run on an unverified artifact. Android boot_completed=1 after the survey; the two completed task-specific 20 GiB staging files were removed, freeing phone space, while the backed-up images and active managed candidate were preserved. All pinned maps must be reacquired and verified after reboot and before booting the new payload.

CI 42569395900 completed successfully at a6fafa62408807bc8730fd02c0125f5ee21ae768; all 40 artifact manifest files verified. Its kernel SHA-256 is 14c6933bf0fde4c55ae933cdc82a2a1e499f8055882ac31e6a72c357d2265887, unstripped 044cc649bc40305041c146541f786ae02222411db37fed379d0e1399c450096f. The log confirms both 0046 and 0108 patches applied and SweetSurface.cpp compiled. After fresh map validation, locally packed boot loaders: survey bf60e03a2343a353e8324a73c59c6c9ed454609b95c2a90f6253d193e4b950ff, smoke d45ec7754315fad6729240d523a3e547c72298e250faddaac26eb7949681b3ad. New kernel GPU smoke stage 0–6 passed with `done PASS` (native-surface-fixed-smoke-boot-1). After Android reboot and new map/payload verification, native-surface-fixed-survey-boot-1 showed backboardd admitted and 24 stored GPU frames, then first refusal `newTextureWithBytesNoCopy:length:descriptor:deallocator: is not implemented`; the gate recorded denial and respawned CPU. This establishes GPU composition occurred briefly but not stable UI or physical correctness. The earlier zero-row failure did not recur before that first failure. Android returned boot_completed=1.

To determine the actual raw texture request without guessing pixel layout or touching caller data, NativeMetal patch 0007 adds a bounded metadata-only log of descriptor, length, alignment and deallocator presence, preserving the existing nil/refusal behavior. Public harness commit d141d6829d0db0b56ef16eaf8ce19bceb5f82112 starts iOS build 42578828761 and Mac tests 42578828635, both in progress. Do not deploy unless both pass, artifact manifest and signatures verify, a fresh candidate backup completes and independent host readback matches, and fresh pin/extent/DM/disjointness checks pass. A new backup to E:/Codex-Xiaomi13-GPU-20261109/native-phone-prenocopydiag-backup.img has started via the official mapping; it is incomplete until process exit, exact byte count, stream/device hash agreement and separate readback.

The pre-nocopy diagnostic backup completed at 21474836480 bytes; the original mapped-device hash, streamed data, and independent host readback matched SHA-256 116f8ee316d5c0548304e1ddfd3af788b25353ae1553d38d50a6bda211f3e2eb. The full 20 GiB diagnostic stage transfer completed and phone SHA-256 matched 4fd1cf54c9b5a3c1b6c9215b1601bec179dcb04f6f353629e95442a871af6abd. CI build 42578828761 and Mac test 42578828635 both succeeded at d141d6829d0db0b56ef16eaf8ce19bceb5f82112. Fresh mapping and payload checks passed. The guarded writer is prepared and hash-matched on phone; no diagnostic-root write has occurred at this point.

The guarded write checked the source digest and current mapped-image digest against the independently verified backup, then wrote exactly 21474836480 bytes. Official unmap/remap and full readback matched diagnostic source SHA-256 4fd1cf54c9b5a3c1b6c9215b1601bec179dcb04f6f353629e95442a871af6abd. This is a byte-verified diagnostic image, not evidence of a functioning desktop.

Diagnostic root was booted with the surface-fixed survey loader after fresh map and payload verification. native-nocopydiag-survey-boot-1/boot.log shows GPU backboardd admitted and at least 19 stored frames but no FUXI NOCOPY request or refusal while the phone was unattended; it does not establish a working or fluid desktop. The 19 recorded frames were marked late; idle windows do not establish frame rate. Monitor was stopped after capturing idle output, and a verified unique Fuxi probe was given a one-minute reset deadline. A second bounded survey loader was prepared with only fuxitouchscript=150 and fuxitouchscriptn=2 added, for synthetic gestures; payload/loader hashes and equivalence checked. Reacquire and verify the Android map before booting it.

After Android boot_completed=1, re-mapped and revalidated the candidate, both standard payloads, and the actual diagnostic/touch loader against the fresh FIEMAP; officially unmapped and temporarily booted the touch-survey loader. native-nocopydiag-touch-survey-boot-1/boot.log captured the no-copy refusal before its delayed synthetic gestures began. The request repeated 8 times in bounded diagnostics: length 2752512, page aligned, no deallocator, type 2, Metal format 204 (ASTC_4x4_LDR), 1125x2436x1, one mip/array/sample, storage/cache/hazard 0 and usage 0x1. After 16 stored frames, survey logged 29 refusals and CPU fallback. The 4x4 ASTC tight-block size is 2747808 bytes, which rounds to 2752512 at 16 KiB, but equality does not prove the Apple raw-memory layout, pixel contents, mutability, or lifetime. Do not treat it as linear without direct evidence. Delayed touch script had not begun at capture. Monitor stopped after survey summary, verified probe reset deadline set to one minute, and Android boot_completed=1 with managed candidate mapped=false confirmed. Diagnostic boot modified the candidate, so its preboot SHA is not a current backup. User was away; physical display and interaction remain unverified.

Format 204 is identified by the pinned Metal enum as ASTC_4x4_LDR. D:/xiaomi13/fuxi_ios/UniXNU has a newer no-copy implementation that explicitly accepts DroidVM's own linear layout and warns Apple's layout is unknown; the observed backboardd log did not report a corresponding layout-object request, so this implementation cannot be assumed safe for the observed bytes. Prepared a local public-source-only patch nativemetal-fuxi-0008-nocopy-callers.patch that logs at most 12 caller image-relative stack frames once and retains the refusal/no-ownership behavior. Added an observed-ASTC-descriptor refusal test and connected the patch to both existing macOS workflows. Patch application check, workflow YAML parsing, and git diff --check pass locally; no Mac compilation, commit, push, or device deployment of this patch has occurred. Obtain authorization for these specific new public changes before publishing. D: reported zero free bytes; do not create further artifacts there.

While the public CI authorization remained pending, performed read-only offline investigation using native-uchar3-root.img mounted read-only in QEMU; no phone write or reboot. nocopy-selector-inspect.log found occurrences of both no-copy and texture-layout selectors, but no plain 64-bit direct selector pointers in the scanned cache files. nocopy-selector-images.log maps selector strings to system image text ranges, including QuartzCore for both, ImageIO for layout, and Metal/IOGPU; this is a string-location result only, not proof of the runtime caller, memory layout, or that a method was invoked. The prepared bounded caller-stack diagnostic remains the concrete next step pending permission to publish/build. Android boot_completed=1 was reconfirmed; D: remains full and E: has free space.

Read-only QuartzCore analysis advanced beyond selector strings. In the local iOS cache, decoded its objc selector-reference fixups and 32-byte stubs, identifying a direct static newTextureWithBytesNoCopy call at unslid VA 0x188483ffc and newTextureLayoutWithDescriptor at 0x1884ccccc. Around 0x188483fa4, a bit in the source object at +0xa8 selects two distinct paths: bit clear passes [object+0x58] as raw pointer, computes a rounded length, and passes a nil deallocator to no-copy; bit set creates a texture and calls replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage: for its levels, using offsets and per-level row bytes from the object. This explains why a generic tightly packed assumption is unproven, and provides a possible future direction to investigate; it does not prove the traced static call was the runtime instance, source byte layout, or that changing the branch is safe. See E:/Codex-Xiaomi13-GPU-20261109/nocopy-quartz-functions.log and nocopy-quartz-function-disassembly.txt. Enumerated candidate +0xa8 byte stores in nocopy-quartz-stores.log; object identities are not established. Android is connected and managed candidate is unmapped. No phone mutation or public submission occurred in this offline investigation.

Further read-only analysis linked the same compressed-source constructor at 0x18837cd78 to two static producers. Around 0x188358b00, a layout object aligns/initializes destination memory and copies linear data through copyFromLinearBytes:...toTextureMemory: before constructing the source; at 0x188358d0c it sets source flag bit 0 to the inverse of transformed-memory availability. Around 0x18849db74, a separate decoder obtains layout size/alignment, copies a pre-existing byte span, constructs the same source at 0x18849dc84, and sets bit 0 from serialized metadata at 0x18849dcec. These static paths explain why layout producer/consumer agreement and cache invalidation may matter; they do not identify this boot's caller or prove its byte layout. An evidence note and repair constraints are in E:/Codex-Xiaomi13-GPU-20261109/nocopy-layout-investigation.md; no source payload or device data were published. Android boot_completed=1 and managed candidate mapped=false rechecked. Public CI for the prepared stack diagnostic remains pending the user's authorization.

Additional read-only cache analysis found QuartzCore's layout helper 0x1884ccc68 is called directly by both the conversion producer (0x188358aec) and the decoder (0x18849db74). ImageIO has a separate layout path: the function at 0x18bb03294 sends watermark to a layout object and returns its two-word identifier. Its call at 0x18bafe6e0 compares both words against input metadata and branches on mismatch. This establishes an identifier comparison for that static ImageIO path, not that every cache validates its layout or that the observed device buffer is compatible. See E:/Codex-Xiaomi13-GPU-20261109/nocopy-layout-investigation.md and nocopy-watermark-xrefs.log. All investigation was offline/read-only; the diagnostic patch is still local and awaiting user permission for public submission and remote CI.

During review of a potential ASTC repair, found the existing frontend computes row*pixel-height for nmb_texture_replace/get_bytes, whereas NativeMetalObjects.rs constructs a Rust slice of that size; for the observed 1125x2436 ASTC example with tight row 4512 this would claim 10991232 bytes although the block-copy plan reaches 2747808. This is a code-derived safety risk, not an observed failure in the diagnostic boot (no-copy was refused first). Prepared isolated prototype E:/Codex-Xiaomi13-GPU-20261109/compressed-copy-span-prototype: runtime-copy-span.patch and native-copy-span.patch validate the runtime copy plan and use its checked exact span in the frontend and bridge, retaining the existing unknown-layout refusal. Both apply checks passed; two actual runtime ffi_span_tests passed on a Windows-only adapted isolated source copy, and bridge objects cargo check passed with unrelated Windows shims. An extracted HostLayout test also passed. Objective-C/iOS build, guard-page/actual GPU copy, 3D convenience overload, pixels, concurrency, and device remain unverified; Windows shims are excluded from patches. No CI integration, publication, or device deployment occurred. See prototype README. The original stack diagnostic CI authorization remains pending.

On a local NVIDIA Vulkan device, a Windows-only guard-page test of the isolated bridge prototype passed for an 8x8 BC1 block texture. It checked required span, rejection of short input and output, data round-trip with both buffers ending at inaccessible pages, padded rows and preserved padding. Runtime logs say CPU block decoding and a decoded image twin were used; this is not proof of ASTC, actual Fuxi GPU decoding, or the Objective-C frontend. Both Windows GPUs report textureCompressionASTC_LDR=false. The test lives in compressed-copy-span-prototype/bridge-work/guard_page_test.rs and is not part of the portable patches. No phone write or public submission occurred; iOS build and actual device validation remain outstanding.

Prepared isolated ci-work integrating the prior bounded caller diagnostic and the compressed-copy-span prototype. The builder applies metal2vulkan-fuxi-0001-copy-span.patch after the optional draw stack independently of FUXI_NATIVE_M2V_DRAW, and archives the patch recipe; the host workflow runs ffi_span_tests. Workflow YAML parsed. Full prepare-only replay of pinned source/patch stacks passed twice and a separate clean worktree passed with draw patches disabled. The first Windows attempt failed because native bridge patch files were checked out as CRLF; normalizing only those files in the isolated checkout fixed it. ci-work has not been committed, pushed, built on Mac or deployed. The earlier diagnostic authorization remains pending; any approval for this additional memory-span prototype must cover these newly prepared changes explicitly. Android boot_completed=1 and native managed image mapped=false confirmed.

Located the existing QEMU-bundled SwiftShader CPU Vulkan ICD. After setting a child-process-only ICD manifest and DLL search directory, vulkaninfo reported textureCompressionASTC_LDR=true. Two isolated bridge guard-page tests passed on it: ASTC 4x4 block-byte uploads/readbacks for 7x7 and the observed 1125x2436 extent (2747808-byte exact span), including short-buffer refusal, padded rows and unchanged padding. The byte patterns were arbitrary and were not sampled; this demonstrates the local CPU Vulkan copy path, not rendered colors, private no-copy memory layout, Objective-C frontend, or Fuxi hardware. Test log: E:/Codex-Xiaomi13-GPU-20261109/compressed-copy-span-prototype/astc-guard-test.log. The test did not touch the phone and nothing was published.

Prepared E:/Codex-Xiaomi13-GPU-20261109/astc-pixel-smoke-prototype/astc-pixel-smoke.patch: opt-in stage 7 uses known ASTC 4x4 UNORM16 constant-colour blocks, uploads and reads compressed bytes, samples 65536 output pixels, replaces one block and checks 65536 pixels again plus sentinels. It uses existing shaders and leaves default stage and backboard gate unchanged. Constant block helper extracted from proposed smoke was compiled with Windows clang and byte-checked for five colors against the encoding described in ARM astcenc_symbolic_physical.cpp. The offline staging model in Alpine passed with stage 7 accepted and 8 refused before copying. Patch checks and full pinned iOS build workflow patch ordering passed; workflow YAML parsed. Isolated ci-work now includes this additional public-source patch; it has not been submitted, compiled with Xcode, or run on device. It tests standard ASTC sampling, not unknown no-copy layout or stable desktop; actual shader orientation and colors are unverified. Approval for public CI must explicitly cover this new test as well as the earlier diagnostics and span changes.

Independently built Arm astcdec 5.7.1 from commit a796fda5348bc007a215a781ebe83ebe4d184f84 in E:/Codex-Xiaomi13-GPU-20261109/astc-pixel-smoke-prototype; initial Windows build hit codepage warnings, rebuilt successfully with UTF-8 and warnings-not-errors. Generated the exact stage-7 smoke's initial and partial-update 8x8 ASTC fixtures using its compiled helper, decoded both with astcdec -dl, and verified every one of 64 RGBA pixels per fixture against the intended red/green/blue/white quadrants and yellow replacement. This validates the compressed test fixtures and expected colors independently; it does not validate Metal shader sampling, iOS compilation, phone GPU or desktop. No phone mutation or publishing occurred; public CI remains pending user consent.

Ran a local full pixel control using the isolated patched Rust runtime, the existing compiled fuxi_quad/fuxi_sample metallib from native-ca-artifact (SHA-256 7c2a182f213be0a80ee5d29c8b397186cad8d9546289c3bebe23a3d7f4bd78d0), SwiftShader CPU Vulkan, the stage-7 ASTC fixtures, and the same render/readback expectations. Both phases passed every one of 65536 BGRA pixels and sentinel boundaries, including the block replacement; log E:/Codex-Xiaomi13-GPU-20261109/compressed-copy-span-prototype/astc-render-test.log, test in bridge-work/guard_page_test.rs. Runtime reported device sampling and texel conversion precision caveats. This validates the test fixture and existing shader behavior on CPU Vulkan, not Objective-C smoke compilation, private no-copy layout or actual Fuxi GPU. Device reconfirmed Android boot_completed=1, candidate mapped=false; no public submission or phone mutation.

A further isolated SwiftShader CPU Vulkan guard-page test passed for a 4x2x3 RGBA8 texture: explicitly supplied 16-byte row and 64-byte image strides, exact 160-byte span, short-source rejection, three slices round-trip, preserved two padding gaps, and refusal of overflowing image stride. Log E:/Codex-Xiaomi13-GPU-20261109/compressed-copy-span-prototype/texture-3d-guard-test.log, code bridge-work/guard_page_test.rs. This covers explicit 3D bridge calls, not the Objective-C convenience overload, iOS or phone GPU. Re-ran test-native-patch-stack.py against the prepared complete source patch sequence; idempotence and rejection controls passed. Android boot_completed=1 rechecked. No phone write or public submission.

Revalidated the patched runtime's host-copy plan for desktop format 550 with an isolated SwiftShader CPU Vulkan test. A 4x4 RGB10A8_2P_XR10 color-plane round trip adjacent to guard pages passed: exact 76-byte span, short-source refusal, 20-byte row pitch with padding preserved, and refusal of an unmeasured 17-byte pitch. See E:/Codex-Xiaomi13-GPU-20261109/compressed-copy-span-prototype/texture-xr-guard-test.log and bridge-work/guard_page_test.rs. This is a color-plane CPU-copy check; alpha preservation, IOSurface import, GPU compositor and phone remain unverified. No phone write or public submission occurred.

Two further tests passed on the isolated SwiftShader CPU Vulkan model of format 550. A color-plane CPU update and readback with guarded buffers preserved the modeled alpha plane and all trailing bytes (xr-alpha-guard-test.log). The existing fuxi_quad/fuxi_sample shader sampled four color/alpha combinations into a 256x256 BGRA target; all 65536 pixels and sentinels matched, and source bytes were unchanged (xr-sample-test.log). The runtime used a shadow texture for a buffer-backed two-plane model. These tests do not establish real IOSurface import, the phone GPU's behavior, or desktop rendering. Android serial <序號> was detected via ADB and sys.boot_completed=1; physical screen and interaction were not observed. No phone write or public submission occurred. Asked the user for explicit authorization to publish the prepared caller diagnostic, copy-span correction and opt-in ASTC smoke together and run the Mac tests/build; that answer is pending.

An isolated CPU Vulkan control now uploads known ASTC blocks at the observed 1125x2436 size from an exact 2747808-byte guard-page-adjacent buffer and samples them with the existing smoke shader into 257x257 output. All 66049 pixels and sentinels matched, including separately colored final block row and partial final block column sampled by clamp (astc-observed-size-render-test.log). The first version's full-resolution expectation failed because the shader divides pixel position by 256, not the output dimensions; the revised test explicitly matches that shader. Re-ran the earlier ASTC and modeled XR shader tests after restoring them; both passed (shader-regression-test.log). This is CPU Vulkan, does not prove actual device GPU rendering or the private no-copy layout, and no phone write or publication occurred. Public CI authorization remains pending.

Read-only QEMU inspection of native-uchar3-root.img identified shared-cache UUID 8f970452-c9e3-3e88-b114-feb1864bc9e0 and QuartzCore image UUID 9e40b2eb-260c-3af1-ae45-a1a551a8c1b9 with text base 0x188284000 (nocopy-cache-identity.log). Prepared resolve-nocopy-frames.py for a future metadata-only caller trace; it requires a separately verified matching UUID and reports only exact static return-site matches. Synthetic controls passed for exact and neighboring offsets, unrelated image, unavailable frame, bounds, UUID mismatch and malformed input. This identity is for the inspected image, not yet proof of the actual later candidate's cache identity. Caller trace and actual layout remain unknown; no phone mutation or public publication occurred.

Compared native-uchar3-root.img and the host native-nocopydiag-root.img source in read-only QEMU: all 44 dyld_shared_cache_arm64* files had matching names, lengths and full SHA-256, including symbols (nocopy-cache-compare.log). The main cache SHA-256 is 76874e8375252ee643011ff8063f0cce12355f05cf5f9897a34e5679667a65aa. This connects the static analysis to the diagnostic image before boot; it does not prove the current post-boot phone candidate or a future candidate has identical files. No device mutation or publication occurred. Public CI authorization is still pending.

With Android boot_completed=1 and serial <序號>, officially mapped the sole native managed image and verified complete FIEMAP coverage, exact DM mapping, pin and disjointness (39 extents). A new full backup to E:/Codex-Xiaomi13-GPU-20261109/native-phone-precaller-current-backup.img completed at 21474836480 bytes; the pre-stream phone SHA-256, streamed digest and independent host readback all matched 831a1828e3cf6b4b388f70291368f73d627267d45df6119a7ab74dc648232c2b. Official unmap succeeded and mapped=false was confirmed. In read-only QEMU, this current backup and native-uchar3-root.img had identical names, lengths and SHA-256 across all 44 dyld_shared_cache_arm64* files, including symbols (native-precaller-current-cache-compare.log), so the backed-up candidate's cache matches the earlier analysis. This does not establish a future candidate's cache identity, actual caller, layout, desktop GPU performance, or physical display/touch. No phone image write or boot was performed. A fresh mapping and full current hash check are required immediately before any future guarded write. Public CI authorization remains pending.

Created native-precaller-current-overlay.qcow2 over the independently verified current backup; qemu-img check passed and compare initially reported identical bytes. A normal read-only HFS check skipped the journaled volume and was not a valid scan. Forced read-only fsck on the overlay reported that the journal needed replay and an incorrect number of file hard links (native-precaller-overlay-fsck-forced.log); treat the post-boot backup as a recovery copy, not a clean staging base. For control, the pre-boot native-nocopydiag-root.img passed the same forced read-only check (native-nocopydiag-source-fsck.log). On a separate disposable native-precaller-journal-probe.qcow2, plain fsck -y skipped the journaled volume and a byte comparison confirmed no change; forced fsck -fy changed only that disposable overlay, reported hard-link repairs and its own successful recheck, but a subsequent independent forced read-only scan still reported an unreplayed journal and corruption (native-precaller-repair-probe.log). Therefore the repair experiment failed acceptance: do not use that scratch overlay or infer data integrity. The original phone image and verified backup were not modified. Retain the clean pre-boot base and CPU fallback; resolve journal replay/clean staging and reverify actual current hash before any future write.

User explicitly approved submission and testing of caller diagnostics, compressed-copy-span patch and opt-in ASTC smoke, and said future work on this task need not repeatedly ask. Public harness commit 984e0ef7a53c025a8fb9bfe1298cb9203e6d612c was pushed; iOS build 42601737848 and host tests 42601738069 are running at that exact commit. Inspect both results, artifact manifests, signatures and hashes before any deployment.

Located the existing iOS source hfs-journal-replay.py. A local block-device adaptation only obtains device size by ioctl when fstat returns zero. On the read-only verified backup, its dry run validated the journal header and reported 16 block lists, 904 blocks and 6864896 bytes (native-precaller-journal-dryrun.log). A second disposable overlay over the untouched backup was replayed with that helper; journal then reported empty, but forced read-only fsck still found hard-link count errors. On that disposable overlay only, forced repair then reported success; a separate forced read-only scan reported 'appears to be OK' and the journal remained empty (native-precaller-replay-repair-probe.log). A read-only comparison of all 44 system cache files against the previously analyzed source passed (native-precaller-replayed-cache-compare.log). This is not proof all user files survived recovery, and the original phone image and backup were never modified. Do not deploy the recovered overlay without full file-content and hard-link assessment, fresh device backup/hash/map validation, and CI artifact verification.

At user direction the complete set was committed and pushed. Host CI at 984e0ef failed because the old invalid-stage test still considered stage 7 invalid. Updated the test to reject stage 8 and confirm 7 reaches the host stub's stage-4 failure; commit 4506b26a11f23273c83f97bc8e8ee8af9172b047 was pushed. iOS build 42603533992 and host test 42603534837 both succeeded at that revision. The host log includes actual NativePortTests and both ffi_span_tests PASS. All 19 downloaded iOS artifact manifest entries matched; logs confirm signed iOS arm64 artifacts. Artifact location E:/Codex-Xiaomi13-GPU-20261109/caller-span-ci-4506b26-artifact.

The recovered overlay's tree was compared read-only to the unreplayed backup: 185318 readable regular files, 143666 directories, 2329 symlinks, and hard-link group sets matched; only the journal file differed. Two Thai-named system shortcuts could not be looked up through Linux HFS+ on either side; 7-Zip independently read each exact shortcut from both raw images and their lengths and SHA-256 matched (native-precaller-special-files-verify.log). This comparison did not exhaustively compare extended attributes or prove iOS runtime behavior. The original backup remains untouched. An isolated child of the recovered image was staged with the signed 4506b26 artifacts, ASTC smoke stage 7, and backboardd not admitted; SHA-256 of staged binaries matched and forced HFS checks passed before and after (native-caller-smoke-stage.log). A raw export compared identical to that overlay and SHA-256 is 0ed800cc6e8972146c6fb89a20743260b5f301b699bb6b6bd22e965ba120c52f. Transfer to /data/local/tmp/codex-native-caller-smoke-stage.img is in progress in host session 7638; a partial transfer is unusable. After transfer, verify phone SHA, then reacquire/verify managed map/payload and check current mapped-image hash equals backup 831a1828e3cf6b4b388f70291368f73d627267d45df6119a7ab74dc648232c2b immediately before any guarded write; ensure official unmap/remap readback and fresh payload map verification before a bounded temporary boot. Preserve CPU fallback. No candidate write has occurred yet.

The initial adb push disconnected after 3106537472 bytes; the full partial file's phone digest matched the same source prefix d26cc9e1c58b275a776f22f8066dbccc615cc20b5100bcd6cd5c94e409c9573a. A resumable task-scoped transfer (resume-caller-smoke-transfer.py, host session 9454) is now appending independently hash-checked 256 MiB chunks to that verified prefix; it verifies each appended tail and can recover from disconnects. Do not use the stage file until process success, exact 20 GiB and full phone SHA-256 match. An attempted extended-attribute comparison was inconclusive: the Linux driver returned OSError on 328986 paths and exposed zero attributes, so the matching error sets and its original INSPECT_SUCCESS marker do not prove xattr integrity (native-precaller-replayed-xattr-compare.log). The local comparison script now fails on insufficient coverage. Structural HFS fsck passed, but full extended-attribute equivalence remains unverified. The intact pre-write phone backup is retained.

Resumable transfer completed with each chunk verified; exact phone stage size is 21474836480 and its full SHA-256 matched 0ed800cc6e8972146c6fb89a20743260b5f301b699bb6b6bd22e965ba120c52f. Official native map, pin, DM/FIEMAP, disjointness and actual smoke loader/payload were freshly verified. Guarded writer and a no-hangup supervisor were copied to task-specific phone paths and independently hash-verified. The supervisor was started exactly once and is currently alive as phone PID 14601; its control directory is /data/local/tmp/codex-native-caller-write-control, with pid/log/status files. It first checks the writer and then the stage SHA and current mapped target SHA against the independently verified backup 831a1828e3cf6b4b388f70291368f73d627267d45df6119a7ab74dc648232c2b before writing. Absence of status while PID is alive means it is still running; never rerun while alive or after an ambiguous disconnect. After terminal status 0, officially unmap/remap and perform full readback hash before removing only the task-specific staging file; fresh reverify mapping and loader before temporary boot. For failure or interruption, retain the stage and intact backup and assess actual target before any recovery. No claim of completed write yet.

The phone supervisor subsequently exited status 0 with dd reporting exactly 21474836480 bytes. Official unmap/remap and freshly checked map succeeded; a complete mapped-device readback SHA-256 matched 0ed800cc6e8972146c6fb89a20743260b5f301b699bb6b6bd22e965ba120c52f. The task-specific phone transfer file was removed, mapping and smoke loader reverified, then the image officially unmapped; status reports mapped=false. Android remained boot_completed=1 at the last check. The 4506b26 test root is byte-verified but has not yet been booted; ASTC and physical desktop remain unverified. The independently verified pre-write backup is retained. For a bounded smoke boot use native-surface-fixed-smoke-loader/boot.img (d45ec7754315fad6729240d523a3e547c72298e250faddaac26eb7949681b3ad), its payload.bin, and kernel-surface-artifact/kernel.development.fuxi.unstripped (044cc649bc40305041c146541f786ae02222411db37fed379d0e1399c450096f). Ensure no monitor is running, capture stage-7 result, and restore probe reset to one minute after stopping monitor; upon Android return all maps must be freshly reacquired. Backboardd is not admitted in this smoke root.

Booted the byte-verified 4506b26 smoke root with the verified surface-fixed smoke loader. The first monitor invocation had no libusb backend and did not hand off; adding D:/xiaomi13/fuxi_ios to the child PATH fixed it, and exactly one Fuxi probe was monitored. Actual phone log E:/Codex-Xiaomi13-GPU-20261109/native-caller-astc-smoke-boot-1/boot.log shows stage 0–6 PASS and stage 7 ASTC PASS: compressed roundtrip, four colors, partial update, 131072 sampled pixels and sentinel edges, followed by done PASS. This proves the tested offscreen device GPU path, not unknown no-copy layout, GPU desktop stability, frame rate, or physical screen/touch. After capturing the terminal result the unique monitor was stopped, probe identity verified and reset deadline shortened to one minute. Android returned with sys.boot_completed=1. The boot modified the managed candidate, so the preboot hash is not a current backup; before any further guarded write obtain a complete verified current backup and freshly revalidate mapping/extent pin/disjointness. To gather the pending metadata-only caller stack, prepare a new survey image with backboardd admitted only in the existing bounded survey gate and keep the CPU fallback. Do not infer no-copy layout or owner from this ASTC PASS.

After Android return, official mapping and verify-native-map.py passed again (39 extents, pinned/disjoint). A new backup of the post-ASTC candidate is in progress to E:/Codex-Xiaomi13-GPU-20261109/native-phone-postastc-backup.img using backup-native-postastc.py, host session 36976 and native-postastc-backup.log. The partial backup is not usable; wait for terminal status, exact 20 GiB and phone/stream digest agreement, then separately hash the host file. Upon completion officially unmap. Future survey staging templates are in native-caller-survey-share and prepare-caller-survey-guest.py; they use the verified 4506b26 artifacts and bounded backboardd allowance, but have not been applied or run. stage-postastc-replay.sh and inspect-postastc-replay-guest.py are prepared for an isolated child of the new verified backup, not yet run. Fresh pin/map/payload checks and guarded current/source hash checks remain mandatory before any new write or boot.

Post-ASTC backup completed at 21474836480 bytes with phone, streamed and independent host SHA-256 81df842a2d3f1ce64d1fc518755b3a221783a11aab48cbd000877fa3d9c01a25; officially unmapped. On a separate overlay, the existing journal tool replayed 17 lists/705 blocks and forced read-only fsck reported OK without repair (native-postastc-replay.log). Child overlay staged signed 4506b26 artifacts and bounded backboardd survey allowance; forced fsck passed before/after (native-caller-survey-stage.log). Raw export matched overlay and SHA-256 6673d788cad7911ea8349d4527c24d763283b14aceb3d885dc84235f04f0975c. The 20 GiB phone transfer was chunk verified and full phone SHA matched. Fresh managed mapping and diagnostic payload checks passed. Task-specific guarded supervisor PID 25772 completed status 0, writing exactly 21474836480 bytes after source/current-backup digest checks; official unmap/remap and full target readback SHA matched the source. Task staging file was removed, map/loader revalidated, officially unmapped with mapped=false. This survey candidate is byte-verified and has not yet been booted; retained post-ASTC backup is the recovery point. Use native-nocopy-touch-survey-loader with verified matching payload and kernel, a single monitor, bounded reset, and capture metadata-only caller stack. Desktop function, physical display and touch are not proven.

The bounded caller survey subsequently ran on device with the verified loader. E:/Codex-Xiaomi13-GPU-20261109/native-caller-survey-boot-1/boot.log captured backboardd's 1125x2436 ASTC no-copy refusal and metadata-only stack: frame 1 QuartzCore +0x200000, which matches the return at 0x188484000 after the previously disassembled no-copy call at 0x188483ffc. The backing cache identity was established from the earlier candidate and retained system cache; this identifies the consumer call site, not the source, byte layout, mutability or lifetime. The survey observed 22 GPU frames before 58 refusals, then the guarded CPU fallback; this does not establish fluid or correct desktop. Monitor was stopped after the timer summary and verified probe reset shortened to one minute. Android sys.boot_completed=1 and managed image mapped=false were confirmed. Survey boot modified the candidate: before any further writes obtain a fresh complete verified backup and repeat official mapping/extent/pin/disjointness checks. The actual device ASTC sampling PASS remains valid only for the separate offscreen smoke.

Read-only disassembly of actual stack windows is in nocopy-stack-windows-disassembly.txt. Frame 2 directly calls the identified consumer at 0x188483cb0, passing its x25 as the consumer's third argument; frame 3 is a virtual call and does not identify the producer. Static examination of the compressed-source constructor shows it sets bit 0 of object+0xa8 by default, and the direct constructor path at 0x188358a40 clears only bit 1 and skips the later bit-0 update, so absent a later mutation that path would select ordinary upload rather than this observed no-copy branch. Another constructor at 0x18837a660 explicitly assigns bit 0 from a prior boolean, and the separate decode path assigns it from metadata. These are static constraints, not proof which producer ran or what raw data layout is. No new phone mutation occurred.

Prepared a separate host-only preserved-register unwinder control in public harness tests/UnwindRegisterTests.cpp and a dedicated workflow. This tests two known sentinel values at an exact caller return PC, using DWARF register 19 on arm64 or 12 on x86_64; it never reads a system object or phone data. On local Alpine x86_64 under QEMU the test passed (unwind-register-linux-test.log); that does not establish Darwin/arm64 behavior. Public harness commit 491278959370ece7b5320a91248cbc62188001f3 was pushed and workflow run 42623906370 is queued. Inspect its result and architecture before using this idea. Even a successful host control would not establish that QuartzCore's register or a pointer is safe to inspect; any device diagnostic needs exact frame/identity checks, bounded safe memory handling and a separate on-device controlled validation. No phone write or reboot occurred in this work.

Unwind control was strengthened so the callee deliberately overwrites the same preserved register with a different value: Android arm64 passed, disassembly confirmed x19 holds distinct caller/callee values, and macOS arm64 CI 42624920459 at d4fbcbd passed. These are synthetic controls, not iOS runtime evidence. A bounded copied-header/frame identity prototype rejects unknown image path/UUID/return address, duplicate matches, pointer/dimension mismatch and wrong branch; synthetic Android arm64 and macOS arm64 checks passed. A separate Mach-O UUID parser's malformed/duplicate tests passed on both Android arm64 and macOS arm64. A self-memory read control passed on macOS arm64, including a protected boundary; its first iOS build failed because mach_vm.h is unavailable. The vm_read_overwrite variant compiled and signed for iOS and passed macOS tests in CI 42627269817 at 3d120d20f0010c138339dfca17d2a1bc41ebea9f. Combined synthetic unwind and safe-read iOS control was prepared and dispatched in run 42627630196 at 470e017d58d0ca6fa83298c4ca10d711396ba0bc; it is queued. None of these probes inspects actual QuartzCore objects or proves raw layout. No iOS instrumentation has been installed.

Following the caller survey, Android was confirmed boot_completed=1, official managed map and pin/disjointness check passed, and a fresh backup is in progress to E:/Codex-Xiaomi13-GPU-20261109/native-phone-postcaller-survey-backup.img using backup-native-postcaller-survey.py, host session 32819 and native-postcaller-survey-backup.log. It is partial until terminal success, exact 20 GiB, phone/stream hash match and independent host hash; then officially unmap. Preserve prior verified backups and do not run any new write/boot with stale map or incomplete backup.

Post-caller-survey backup completed at 21474836480 bytes; phone source, stream and independently read host SHA-256 all match d2a4ce7f86f510bcd2bf78b74ea54a45797b3df21b16e628d3550afee479c396. Official unmap succeeded; mapped=false. On a separate native-postcaller-replay.qcow2 over this backup, the existing journal tool replayed 17 lists/839 blocks and forced read-only fsck twice reported OK without repair (native-postcaller-replay.log). A separate empty native-provenance-control-overlay.qcow2 child was created and qemu-img check passed; it is not staged or booted. Isolated staging and loader preparation exist in native-provenance-control-share, prepare-provenance-control-guest.py, and native-provenance-control-loader. The loader changes only the former smoke execswap path to /usr/local/bin/fuxi-provenance-control, has no backboardd GPU enable argument, and SHA-256 27b6b850abe14af415cef85c66d9765cb90f2d029f60cdb6c6c86896b15d8551; recheck actual payload and fresh map before use. It is intended for synthetic self-read/unwind controls, not real QuartzCore inspection.

Combined synthetic iOS control CI 42627630196 attempt 1 ended failure because no macOS runner was acquired after multiple attempts; no test executed. A rerun was requested at the same commit 470e017d58d0ca6fa83298c4ca10d711396ba0bc. Inspect the actual attempt status/logs and validate the signed iOS artifact before staging. Earlier CI 42627269817 only validated the standalone safe-read test on macOS and built its iOS binary; it did not run on iOS. Do not use any prior failed or incomplete result as a device PASS.

CI 42627630196 attempt 2 passed at 470e017d58d0ca6fa83298c4ca10d711396ba0bc on macOS arm64, including the combined synthetic controls, and built and codesign-verified an iOS arm64 executable. Downloaded artifact ZIP SHA-256 a8a67f33c8745c4a516cae885030696cab1f804c8edadcd44c8e7671634c615e matches GitHub's artifact digest; its only file exactly matches downloaded binary SHA-256 ea2594c10be2fcf945d3ea30e9aae6588da8e0acc482e2da0679a05c9d29cffd. The executable was staged on the isolated native-provenance-control-overlay.qcow2; HFS forced checks passed before/after (native-provenance-control-stage.log). Raw export compared identical, SHA-256 f6f2d1729116de8f5ea3b53f6564e98ef9ca341d690f39a26900d8975fef2277. Its phone transfer was chunk verified and full SHA matched; fresh official mapping, extents and actual control loader validated. The task-scoped guarded supervisor PID 13490 exited status 0 after checking the staged image and exact backup d2a4ce7f86f510bcd2bf78b74ea54a45797b3df21b16e628d3550afee479c396 and writing exactly 21474836480 bytes. After official unmap/remap, full mapped-device readback matched the source SHA. The task staging file was removed, map/loader revalidated, then officially unmapped; mapped=false. The control candidate has not yet been booted. A bounded temporary boot of native-provenance-control-loader (boot hash 27b6b850abe14af415cef85c66d9765cb90f2d029f60cdb6c6c86896b15d8551) should run only synthetic unwind/self-read controls, not inspect QuartzCore or enable desktop GPU; capture both PASS messages and FUXI PROVENANCE CONTROL PASS, then shorten reset and verify Android return. Preserve the verified backup; any later write requires fresh post-boot backup and map verification.

2026-11-09 logging correction and iOS synthetic controls: First control boot launched the binary but produced no visible PASS or failure through 22:19 because sweetnull did not include the executed program name. Its result remains unknown, not a failure or pass. The exact monitor was stopped, the validated probe reset shortened to one minute, Android boot_completed=1 and official managed status mapped=false were confirmed. Built an isolated logging-only loader that adds the full and possible shortened control process names to sweetnull; payload sha256 c9e4b8def866e0d1e2cb6e332ef1e18a6506f2ac373d586891b06b91d5279e18, boot sha256 64ffcb02942c79cf02d3b88448db0ed4718d11e808664099bc9be1e2db045122. After fresh official map, verify-native-map.py checked FIEMAP/DM, pinned extents and disjointness, and verify-provenance-control-logging-loader.py checked current payload extents and all differences against original. Official unmap and mapped=false preceded the temporary boot. In native-provenance-control-boot-2/boot.log lines 1217-1219 the real iOS arm64 synthetic control reported both native unwinder and bounded self-memory PASS and FUXI PROVENANCE CONTROL PASS. This verifies synthetic controls only; no QuartzCore object was read and no desktop performance claim follows. Stopped matching monitor PID 30948 after PASS and verified unique Fuxi probe deadline shortened to one minute (4117312230 -> 3238885324). Awaiting Android return and official status; candidate may have changed during both boots, so a fresh full independent backup is mandatory before the next device image write. No device image write occurred in this continuation.
Android return after the second control boot was confirmed: adb serial <序號> sys.boot_completed=1; official ManagedNativeImage status exists=true mapped=false. Keep the current modified managed candidate untouched until a fresh full verified backup is needed for a subsequent image write.
Prepared and published a strictly observational no-copy provenance diagnostic at harness commit 94d5818cf020cde4f39ac43e896c6771dbf11e68; public patches/test/workflows only, no phone data. It only attempts observation for the specific descriptor and requires exact QuartzCore path, UUID and frame return, a unique frame, bounded self-memory copies of object metadata (twice, identical), matching source pointer and dimensions, and the observed branch. Unknowns retain the existing no-copy refusal; logs exclude texture bytes and raw object pointers. Field meanings remain provisional; this has not been run on a real QuartzCore object. Full native build run 42635672404 and host integration run 42635672178 target this commit. A faster independent control workflow was added at 3516a8ee807e712819cbb7ec18cbc1c49ae98c7f, run 42635893722. All results pending. Following Android boot_completed=1 and mapped=false, officially mapped the candidate and started fresh backup via backup-native-postprovenance-control.py, host session 65256, log native-postprovenance-control-backup.log. Backup is partial until terminal status, exact length and source/stream digest match, followed by independent host hash. Officially unmap afterward. Do not write or boot based on a partial backup.
CI 42635672404 attempt 1 succeeded at 94d5818cf020cde4f39ac43e896c6771dbf11e68. Artifact ZIP native-provenance-ci-94d5818.zip sha256 cca7944f4ca25b5ec8c54467839473f3fe5ee27ecc698e86fd1f43329a591cff matched GitHub digest; safely extracted to native-provenance-ci-94d5818-artifact and verified all 19 listed files and exact manifest coverage. Native bundle executable sha256 bf012f65d588bfc8eaa922a9eac767f60d081c1f05f5d02a36438c18e4c9afec. Independent macOS arm64 control run 42635893722 at 3516a8ee807e712819cbb7ec18cbc1c49ae98c7f succeeded; exact copied-header matching, UUID parser, bounded self read, unknown caller refusal and bounded image reads passed, plus iOS arm64 compile. Host integration with added nil-deallocator no-copy refusal was submitted at 3b35485, run 42636385664; prior host run 42635672178 and new run still pending. These do not prove the real object or desktop behavior. Post-control backup remains in progress.
Fresh post-provenance-control backup completed exactly 21474836480 bytes: phone source sha256, streamed digest and separately re-read host sha256 all match 529e4f0193e5308b2de8a5a6bfef952ad3d3d3669c9329aa75b77eb8c41ab61f. The backup is native-phone-postprovenance-control-backup.img; official unmap and status mapped=false confirmed. CI host integration 42636385664 at 3b35485f55447fc88ec6ab29375dafb4eede0df0 succeeded, including nil-deallocator observed-descriptor refusal for unknown caller, bounded observation, full native frontend tests and 2 ffi_span_tests. Earlier duplicate host run also succeeded. For the downloaded iOS artifact, build audit reports iOS arm64 min16, signed, no CTR_EL0 reads, and correct bridge load. Independently checked packaged FuxiNocopyObservation.h matches prepared source. No diagnostic device image has been staged or written yet.
On isolated overlay native-postprovenance-replay.qcow2 over verified backup 529e4f..., replayed HFS journal 17 lists/355 blocks; forced fsck twice OK and qemu-img check passed. A separate child native-provenance-survey-overlay.qcow2 was staged with the verified 94d5818 iOS CI artifact and backboardd survey configuration; guest fsck before/after succeeded, installed native binary sha256 bf012f65d588bfc8eaa922a9eac767f60d081c1f05f5d02a36438c18e4c9afec matched artifact. Raw export native-provenance-survey-root.img compared identical with overlay, is exactly 20 GiB and sha256 6e6e90c5a1a93d5c16ace55cfacead65b66ed5f03cbe79125d002ba27504c224. Android boot_completed=1 and official mapped=false; prechecked 70 GiB phone free and absent task-specific staging paths. Created only empty /data/local/tmp/codex-provenance-survey-stage.img and launched transfer-provenance-survey.py, session 9946, log native-provenance-survey-transfer.log, with chunk hashes. Transfer is partial and is not usable until all 20 GiB and full phone digest verified. Exact-target/source/current hash guarded writer and supervisor are locally prepared, NOT run. Before any write perform fresh official map, verify-native-map.py, verify-surface-survey-payload.py and loader/hash checks, recheck current candidate still has fresh backup hash, then guarded writer, full unmap/remap readback and final unmap. Preserve existing backups and do not boot incomplete results.
IMPORTANT supersession: During review, field +0x60 interpretation was provisional and could be a pointer rather than a size. The old 94d5818 diagnostic printed its numeric value, so it must not be deployed. Stopped only transfer-provenance-survey.py and its task-specific adb child while partial, confirmed no remote task transfer process, then removed only its own incomplete /data/local/tmp/codex-provenance-survey-stage.img; no writer was run and the phone candidate remains at the backed-up post-control state. The local old staged overlay/raw and old CI artifact are obsolete and must not be used. Removed the unknown field from log output and restricted symbolized cleanup paths to /System/Library/ or /usr/lib/. Revised public patch is commit 8091432, iOS build run 42640654655 and host integration run 42640654624 pending. Full fresh backup 529e4f... remains valid unless candidate changes; official mapping was previously unmounted. After exact revised artifact and tests pass, stage it in a NEW child overlay over the existing journal-replayed copy, verify, export, transfer under new paths and repeat all fresh checks before any device write.
Revised 8091432 iOS build 42640654655 succeeded; GitHub artifact ZIP native-provenance-ci-8091432.zip sha256 f99ee7ff70c50b779f2cc0dff4aef591f0c059d0ba1dbb44daf54f6488c13ed1 matched service digest; all 19 files checked. Packaged frontend and observation header exactly match reviewed local source. New native bundle sha256 f7e762f69a916594db0565b6cb7220072a70e81f246e30cf0a4cfab2081c1769. Staged only in new native-provenance-survey-v2-overlay.qcow2 over journal-replayed verified backup; guest recorded bundle hash and fsck OK before/after. Export native-provenance-survey-v2-root.img is 21474836480 bytes, compared equal to overlay, sha256 3391e0376f90337e97ce22f850dc450e8e4d0fe7da7a58b6bceb13442f05e572. Android boot_completed=1, official status mapped=false, staging paths absent and ~70GiB free; created only /data/local/tmp/codex-provenance-survey-v2-stage.img and launched chunk-verified transfer in session 24374, log native-provenance-survey-v2-transfer.log. Transfer incomplete; full target hash and host CI 42640654624 must succeed before any write. Fresh official map, verify-native-map.py, verify-surface-survey-payload.py, pinned non-overlap, loader and target/source/current hashes and guarded writer remain mandatory. No device image write has happened in this continuation.
V2 transfer completed in 80 hash-verified 256 MiB chunks; a couple of transient ADB retries recovered, and independent phone SHA-256 matched exported image 3391e0376f90337e97ce22f850dc450e8e4d0fe7da7a58b6bceb13442f05e572. Fresh official map/verify-native-map.py and verify-surface-survey-payload.py passed. First attempted background supervisor unexpectedly ran as shell because the remote shell placed '&' outside su; its root guard immediately returned status 1 with empty log, before writer execution. No writer remained, and independent full mapped-target SHA-256 confirmed candidate still matched verified backup 529e4f0193e5308b2de8a5a6bfef952ad3d3d3669c9329aa75b77eb8c41ab61f. Preserved the failed control record. Verified correct quoted root execution, revalidated fresh map and payload, uploaded and hash-checked unique v2b supervisor 8d218e0fb39902f4812589f81651a98b44bf01f6e400f410af173f781f6df373 and writer 84243dbcda2c1dd28f946c01b480741495fe9c31f927c9bb1ac9ee265daf1360. Launched correct root supervisor PID 16121, control /data/local/tmp/codex-provenance-survey-v2b-write-control. It is currently in progress and validates exact full source and target hashes before its 20 GiB mapped-device write. Check status/log until terminal; do not interrupt, remap, boot, or assume success. On status 0 and exact dd byte count, official unmap/remap and full readback must match 3391e0..., then remove only v2 staging and its task scripts, rerun mapping and loader verification, formally unmap and confirm mapped=false before any temporary boot.
V2b root supervisor PID 16121 exited status 0 with 5120+0 records and exactly 21474836480 bytes written. Official unmap/remap succeeded; fresh verify-native-map.py and verify-surface-survey-payload.py passed, then full mapped readback sha256 3391e0376f90337e97ce22f850dc450e8e4d0fe7da7a58b6bceb13442f05e572 matched source. Removed only named v2 temporary source and writer/supervisor scripts, then repeated fresh map and loader verification. Official unmap and status mapped=false confirmed. Candidate now contains 8091432 diagnostic, has not been booted; retained post-control backup sha256 529e4f... is recovery point. Use only verified native-nocopy-touch-survey-loader (boot sha256 5315233ff93d9c6528f014525283d3440ebae8f6cc95eb268e4a77be9b0aff58), one monitor with short reset, bounded observational metadata capture, then shorten reset to one minute and verify Android. Any subsequent device image write requires a new full post-boot verified backup and fresh mapping.
After verified V2 write/readback/unmap, temporarily booted verified native-nocopy-touch-survey-loader with one monitor, native-provenance-survey-boot-1. It captured backboardd QuartzCore +0x200000 and the same 1125x2436 ASTC no-copy rejection. At boot.log:2707, the revised live diagnostic reported 'identity or safe read did not match'; no real object metadata was accepted, so layout, producer and GPU desktop success remain unproven. This undifferentiated reason could be stack limit, frame/UUID, register, bounded read or object validation; do not infer which. Stopped matching monitor PID 26692, uniquely checked probe protocol and shortened reset from 5849952784 to 4801893052 (one minute); waiting for Android return and mapped=false. New boot changes candidate: before ANY subsequent image write acquire a fresh full independent SHA-verified backup and redo map/payload/target checks. The preboot backup remains available for recovery. No physical screen/interaction evidence collected.
Post-survey Android boot_completed=1 and managed mapped=false confirmed. User reports the experimental screen remains very laggy; this is consistent with the captured no-copy refusal, not proof of a completed GPU fix. Prepared 7a06fe86942c45d29aa30fa039b55ec4125e2944 with a bounded 128-frame cap and non-sensitive failure codes: 1 stack limit, 2 frame count, 3 image read, 4 identity/register, 5 object read, 6 inconsistent copies, 7 invalid args, 8 short header, 9 pointer mismatch, 10 dimensions mismatch, 11 alternate branch; no raw addresses/unknown fields are logged. Existing refusal remains. Independent Mac host controls and iOS compilation run 42648321782 succeeded; iOS build 42648322186 and host frontend integration 42648322218 are pending. Mapped officially and started fresh full post-survey backup session 1223, native-postprovenance-survey-backup.log, path native-phone-postprovenance-survey-backup.img. It is partial and unusable until complete with full source/stream hash agreement and independently re-read host hash, then official unmap. No other device write occurred since survey boot. Prior v2 backup retained.
Post-provenance-survey backup completed 21474836480 bytes; source phone, streamed and independent host SHA-256 all equal 2f4c71feb78b3a3d57fe6ec28216758757ed7b5830118d964e094e9a5e7545ae. Official unmap/status mapped=false. V3 quick control run 42648321782 passed at 7a06fe8. Full iOS 42648322186 and host 42648322218 attempt 1 ended without executing any steps because GitHub could not acquire macOS arm64 runners after multiple attempts; both rerun attempt 2 and now in progress. Do not infer test success from scheduling. Prior verified 8091432 device observation failed closed with undifferentiated reason. New V3 has no device deployment yet.
V3 commit 7a06fe86942c45d29aa30fa039b55ec4125e2944: independent controls 42648321782 passed, iOS build 42648322186 attempt 2 succeeded and host integration 42648322218 attempt 2 succeeded. Artifact ZIP SHA-256 d977565e822bfa4fb842db6f79c43578d04294ce271f623b42916c44d1bf0a34 matched GitHub; extracted and verified all 19 manifest files, and packaged source matched reviewed V3 files. FuxiNativeMetal executable SHA-256 a4c860af664817b5d1be9c7c13cf4e501c837fa4ccad1e2b713130f7c697a10b. On a separate overlay over the fresh post-survey backup, replayed 17 HFS journal lists/358 blocks and read-only fsck twice OK; V3 child staging recorded exact bundle hash and pre/post fsck OK. Export native-provenance-survey-v3-root.img is exactly 21474836480 bytes, byte-equal to overlay, SHA-256 d1b5449d4b37657375b783d66be36476b1b01a39e3c34c5c282f21d3c92de0f3. Android boot_completed=1 and managed mapped=false; started chunk-verified V3 transfer session 87581, log native-provenance-survey-v3-transfer.log, to unique /data/local/tmp/codex-provenance-survey-v3-stage.img. This is partial until complete and full independent phone SHA-256. No V3 phone candidate write yet. V3 guarded writer and supervisor are only prepared locally; verify exact hashes, fresh map/payload, full target/source/backup hashes and root quoted launch before writing, then official unmap/remap and full readback + cleanup + map/payload verification + unmap before boot.
V3 transfer completed in chunk-hash-verified segments; independently hashed phone stage matches d1b5449d4b37657375b783d66be36476b1b01a39e3c34c5c282f21d3c92de0f3. Official fresh mapping, FIEMAP/DM/pin/disjointness verify-native-map.py and survey payload/loader verification passed. Pushed unique writer SHA-256 ef9b9d549f0b73dcd48c9a425c5f32ab7b47d8387f88faecc4f2873e10756c60 and supervisor 944aea46096bd6dc24160900adfedff3932cd4c6d0c7a72ba864ee9296036f0c; verified phone hashes and started via correctly quoted root shell. Control /data/local/tmp/codex-provenance-survey-v3-write-control, PID 3185, is currently running: it verifies full staged source and current target against backup 2f4c71feb78b3a3d57fe6ec28216758757ed7b5830118d964e094e9a5e7545ae before exact mapped write. Do not interrupt or assume success. Check terminal status and 20 GiB count, official unmap/remap and full readback, task staging cleanup, repeat map/payload verification, final official unmap before boot.
V3 guarded writer PID 3185 exited status 0 with 5120+0 records and exactly 21474836480 bytes. Official unmap/remap, fresh FIEMAP/DM/pin/disjointness and payload checks passed; full mapped readback SHA-256 d1b5449d4b37657375b783d66be36476b1b01a39e3c34c5c282f21d3c92de0f3 matched source. Removed only named v3 staging and writer/supervisor scripts, then repeated mapping and loader checks; officially unmapped and confirmed mapped=false. V3 candidate is ready for one bounded temporary boot with native-nocopy-touch-survey-loader; use one monitor, capture FUXI NOCOPY META validation code, then stop exact monitor, shorten reset to one minute, verify Android and mapped=false. Backup 2f4c71... is retained; a new post-boot full verified backup is mandatory before any subsequent candidate write.


2026-11-12 paired ASTC layout backport (user approved proceeding after research): isolated worktree %USERPROFILE%/Documents/Codex/xiaomi13-layout-backport-20261112/harness, branch codex/astc-layout-backport, source candidate commit cba2ed29f61041eab5c387929a274c53c362b207; runner-input commit 9801b2d679b63a833f2d82a53657ca02e3a679a9. New nativemetal-fuxi-0011-linear-layout.patch adds a matched ASTC4x4 layout producer and ShaderRead snapshot importer; it only admits producer-registered allocations with complete block coverage and matching format/shape. A unique watermark avoids confusing Apple/DVM cache data; unknown serialized allocations remain refused. Successful layout logs use NSLog, not the NBLOG fault hook. Rollback: FUXI_NATIVE_LINEAR_LAYOUT=0. Existing iOS16 base, ABI, completion, copy-span and CPU fallback remain. Windows MSVC ASan portable tests passed. CI 43056824434 at 9801b2d passed Linux ASan/UBSan, previous observation controls, and macOS/iOS16 full frontend syntax checks. Full host 43056830679 and iOS build 43056837931 are running on standard macos-15-intel after ARM64 runner queues; earlier superseded cba2ed2 runs were cancelled. No completed host execution or iOS artifact claimed yet. Check live results/logs and verified artifact source/hash before any deployment. Research and implementation notes, exact source files, delegated Gemini arithmetic output (model verified), local logs and verification scripts are under the isolated task root. Gemini integration review requests timed out twice; main agent reviewed integration itself. ADB devices was empty. No phone image write, mapping, or boot occurred; the last V3 device candidate and its historical backup are unchanged by this continuation.

2026-11-12 follow-up: iOS build 43056837931 at 9801b2d passed. Downloaded artifact 13873825708 ZIP SHA-256 d91e9407f1371a508ea85b1f1ec6ef3195f5d184c3a5c57c798a9ce278896298 matches GitHub digest; all 19 manifest files and native frontend/FuxiBlockLayout.h/FuxiTextureLayout.inc exactly match reviewed files. Stored under %USERPROFILE%/Documents/Codex/xiaomi13-layout-backport-20261112/ios-artifact. Intel host 43056830679 failed before new layout execution, in unchanged CensusTests.mm with SIGILL. Diagnostic 43061440186 reproduces exit132 using original 4b8fade census without layout patches; CoreFoundation forwarding sees wrong object/selector, consistent with using non-stret forwarding on x86_64 large returns. Full host acceptance now fixed to ARM64; ARM64 run 43060844001 at 9801b2d is building. Temporary diagnostic workflow job was reverted. Current branch bec762e has identical patches/tests to 9801b2d; only full-host runner selection differs. No phone connected, written or booted.

2026-11-12 ARM64 host run 43060844001 at 9801b2d completed success. Actual new producer/provenance/importer tests passed under ASan/UBSan including complete block coverage, exact snapshot bytes, rollback, upload failures, unreadable-page refusal and owner lifetime. Existing frontend/gate/callback tests and 2 ffi_span_tests also passed. Full log ci-43060844001-arm64-host.log retained. Signed iOS16 arm64 candidate verified earlier; executable SHA-256 cd801fcb4df3597d50bee02e819cc0ea4ec55077afbbc0c1435fd3745f5f3aab. Candidate source/test bytes remain identical to 9801b2d. Source and host qualification complete; hardware GPU sampling, producer/cache path and desktop acceptance remain pending because ADB, fastboot and Fuxi probe were not detected. No phone write or boot.

2026-11-13 (Claude main agent) layout0011 device deployment and survey. Phone mapped SHA-256 equalled V3 root d1b5449d... (E:/Codex-Xiaomi13-GPU-20261109/native-provenance-survey-v3-root.img), so that file was the verified pre-write backup. Staged the 0011 iOS artifact (FuxiNativeMetal cd801fcb...) on native-layout0011-overlay.qcow2 over the clean V3 raw; guest fsck OK before/after (native-layout0011-stage.log); export native-layout0011-root.img is 21474836480 bytes, SHA-256 15dc5859a0bf108797346b2115c049a8a7d0c3408a2a01b0ef35c6295f7349c6. New tool delta-stage.py copies the mapped device to a phone stage file, pushes only differing 4 MiB blocks (25 blocks/100 MiB) and verifies the full stage SHA (native-layout0011-transfer.log); it never writes the mapper. Guarded writer 58a3b2c7.../supervisor 1b7912ad... (control /data/local/tmp/codex-layout0011-write-control) exited status 0 with exactly 21474836480 bytes; official unmap/remap and full readback matched 15dc5859...; stage removed; map, loader payload and kernel artifact re-verified; mapped=false before boot. Booted verified native-nocopy-touch-survey-loader once with one monitor: native-layout0011-survey-boot-1/boot.log, native-layout0011-survey-monitor-1.log. RESULT: refusal unchanged. backboardd gate allowed, frames=26, then the same 1125x2436 format 204 no-copy refusal (boot.log ~2700-2709), survey summary distinct=3 refusals=33, next-respawn=CPU. No FUXI LINEAR line appeared in any logged process, although the new binary is the one running (NOCOPY CALL frame 0 offset 0x1aa68 -> 0x1b94c). The monitor ended with a USB error when the bounded reset fired; Android boot_completed=1 afterwards. This boot modified the candidate: 15dc5859... is no longer the phone hash; fresh map/verify and a full verified backup are required before any write.

Root-cause analysis (offline, read-only): dumped QuartzCore __text and resolved objc stubs from native-layout0011-root.img (inspect-layout0011-quartz-guest.py, layout0011-inspect-share, layout0011-quartz-dump.log; analyse with qc-disasm.py dis/xref/sel). The V3 metadata line "flags=0x2 cleanup-image=QuartzCore offset=0x248c04" identifies the release function 0x1884ccc04 (page-aligns object+0x58 and tail-calls a deallocate with the task port). Its only users are the constructor special case at 0x18837cf3c and the caller at 0x18837a628, so the runtime object is built by the function around 0x18837a380-0x18837a6a8, not by the conversion producer (cleanup 0x188374ce8) or the flatbuffer decoder (cleanup 0x18849dd34). That function is a render-server message decoder: it reads dimensions/format/level arrays, a port (0x1883868d0 with type 0x11) and maps it read-only with flags 0x33100001, prot 1/1 (0x18837a574), then sets bit 0 of +0xa8 from a decoded value (0x18837a610-0x18837a680). So the bytes backboardd hands to no-copy are a read-only shared mapping sent by ANOTHER process, already flagged as texture-layout data by that sender. Consequence: 0011's per-process producer registry in backboardd can never match this allocation (producer and importer are different processes), which is why 0011 changed nothing. Neither the old "layout object is not known" refusal nor FUXI LINEAR initialize was ever logged, so the sender did not obtain its layout from NativeMetal in a logged process. OPEN (not yet established): which client process sends the image and which Metal device/layout class it used; tools/fuxi-metal/Frontend.mm line ~1925 implements newTextureWithBytesNoCopy in the FuxiMetal wrapper and must be read next, together with whether that wrapper supplies its own layout object to clients. Likely repair direction (unverified): make the importer accept cross-process data only when the sender's layout is provably ours (same deterministic row-major ASTC layout from our own wrapper/producer in the client), instead of an in-process registry. No fuxibbgpu=1 test was attempted. Gemini delegation was tried twice and abandoned for speed (about 2-3 minutes per tool step); no Gemini output was used.

2026-11-13 (Claude main agent) patch 0012 foreign-linear import. DEVICE RESULT: backboardd GPU composition sustained, no refusal. Root cause closed: the sender is a client that cannot open a Metal device on this port (SpringBoard / poster extensions), so it has no layout object; ImageIO writes the wallpaper snapshot as an ATX file (AAPL signature, HEAD, FILL padding, astc chunk) and QuartzCore sends that mapping to the render server flagged as texture-layout data. Five ATX files copied from the device image (E:/Codex-Xiaomi13-GPU-20261109/texture-cache/*.atx) were checked offline: payload is exactly 282x609 blocks x 16 = 2747808 bytes at file offset 0x4000 and only a row width of 282 blocks makes all 609 rows self-consistent, i.e. plain row-major ASTC 4x4. nativemetal-fuxi-0012-foreign-linear.patch makes fuxiImportLinearTexture accept an allocation with NO producer record when: ASTC 4x4 LDR/sRGB, ShaderRead only, one level and one slice, base aligned to 16384, length covers every block, bounded self-memory read succeeds; bytes are copied into an owned snapshot and uploaded through the 0011 bridge call. Multi-level/multi-slice foreign data stays refused. Rollback: FUXI_NATIVE_FOREIGN_LINEAR=0. Details in harness docs/ASTC_LAYOUT.md.

Source/CI: harness commit f0affa1474720d3782de7e29e9ba9b9d7545d178 on codex/astc-layout-backport, pushed (the commit reuses the previous commit's author identity because this machine has no git identity configured; no attribution lines). All three workflows succeeded at that commit: iOS build 43269213707, host integration under ASan/UBSan 43269220181 (includes new foreign-snapshot tests A-G in tests/FuxiLinearLayoutTests.mm), no-copy observation check 43269226285. Artifact 13966709747 ZIP SHA-256 c54dfb92daca90f3dc84e11138f79cf11ff3721d29b1f84bb3a29a2da785242d matches the GitHub digest; all 19 manifest files verified with no extras; packaged NativeBridgeMetalFrontend.mm, FuxiBlockLayout.h and FuxiTextureLayout.inc are byte-identical to the reviewed local files; FuxiNativeMetal executable SHA-256 79cb084440530ec6c40fa85a5fe91c5c229d86c12a54e47359a0ba2b6b8ba77d (%USERPROFILE%/Documents/Codex/xiaomi13-layout-backport-20261112/verify-ci-artifact-0012.py, artifact-verification-0012.json, ios-artifact-0012).

Deployment used a LIGHTWEIGHT FLOW BY EXPLICIT USER DECISION ("手續太多了 提交作為原點 就好", then chose "skip backup, deploy directly"): NO phone backup was taken of the post-0011-boot state (mapped SHA-256 5d84934f26bb7ec0c6a30287cb35c8db726478f54c03e1181c34af4f605db294); recovery points are the commit and the verified clean host images native-layout0011-root.img (15dc5859...) and native-layout0012-root.img. Kept guards: fresh official map + verify-native-map.py before the write, guarded writer with exact source/current hashes, full readback after. Staged the artifact on native-layout0012-overlay.qcow2 over native-layout0011-root.img (prepare-layout0012-guest.py, native-layout0012-stage.log: guest_exit=0, fsck OK three times, bundle hash 79cb0844...); export native-layout0012-root.img is 21474836480 bytes, SHA-256 dea8de6d4c75219902988a1a3c8f7e0e069b150f8aeb0964da7d7f0b767f0ef5. A single `adb push -z lz4` of the 20 GiB image failed at 3106668544 bytes (native-layout0012-transfer.push-failed.log), so new tool delta-stage-hash.py was written: needs no host copy of the phone state; the phone reports SHA-256 of every 4 MiB block of the mapper (read-only), the host diffs against the new image and pushes only differing blocks (206 blocks / 824 MiB), then the full stage SHA is verified (native-layout0012-transfer.log). Writer b0f4379cc4a8eb4476f6c133790d287dd28c8b39fa56d3018ad7e805deb5b2cb / supervisor d81e827faa45cbda12c7259fba7df8f7ef3920f6f5ec940c742e7a82e90f3c26 (control /data/local/tmp/codex-layout0012-write-control) ran once, status 0, 5120 records; official unmap/remap and full readback matched dea8de6d...; stage removed; mapped=false before boot.

One bounded temporary boot with the same touch-survey loader as the 0011 boot, one monitor: E:/Codex-Xiaomi13-GPU-20261109/native-layout0012-survey-boot-1/boot.log (4725 lines), native-layout0012-survey-monitor-1.log. Evidence (boot.log line numbers): 813 gate new-boot reset; 816 gate allow armed strikes=0 good_frames=120; 1559 first_frame_ms=5957.134; 3197, 3200, 3711 "FUXI LINEAR: foreign snapshot imported format=204 width=1125 height=2436 bytes=2747808 length=2752512 deallocator=0" (these three are the only FUXI LINEAR lines); 3381 gate good frames=120. No "is not implemented", native refusal, command buffer refused, survey summary, gate deny or runtime-failure line anywhere. backboardd frame counter reached 613 with failures=0 on every line (0011 boot stopped at frames=26 failures=33). SpringBoard ran (641 log lines from 1630) and the scripted touch sequence completed (4566-4680). Monitor ended on the usual USB error when the bounded reset fired, monitor_exit=0. Afterwards Android sys.boot_completed=1 and managed status mapped=false.

NOT yet established / open: (1) smoothness. Panel presents at up to 60 Hz (line 76). backboardd counters show late=509 of 613 frames; in the busy windows hz was 20.19 (frames 107->208, 98 of 101 late), 20.87 (277->486, 168 of 209 late) and 15.74 (486->565, 74 of 79 late) with window gpu_ms 10-30 ms and gpu_busy_est_pct at most 44.9; all tex_pool/slab/buf_pool/surface_import counters are 0. Display side (line 4539): 600 frames in 212724 ms, copy avg 5270 us max 17694 us, versus the 0011 CPU-fallback boot (its line 5125) 600 frames in 189671 ms, copy avg 15925 us. So the per-frame framebuffer copy is about three times cheaper, but the composition rate observed is about 20 Hz, not 60. "late" is counted in tools/fuxi-metal/BackboardGate.hpp completed() as `if(wallMS>1000.0/60.0)++late` (read in the mac-frontend-source copy, line 104), i.e. the frame's wall time exceeded one 60 Hz period; the same function can emit per-frame "FUXI MTL: late frame+" detail lines when systemTranslationPolicy().lateFrames (FUXI_MTL_LATE_FRAMES) is on, which was not enabled in this boot and is the obvious next diagnostic. What limits the rate (GPU time 10-30 ms per frame vs. waits) is not established. (2) The user has not yet confirmed what the physical screen and touch looked like. (3) This boot modified the phone image, so it no longer hashes to dea8de6d...; measure it again (and map/verify) before any further write. Leftover from the earlier interrupted backup attempt: native-phone-postlayout0011-backup.partial-20261113.img (8.78 GB, incomplete, not usable). Gemini (antigravity/gemini-3.8-flash via Magpie; model confirmed in its transcript) was delegated a read-only evidence extraction of this boot log (output native-layout0012-survey-boot-1-evidence.md, response gemini-bootlog-0012-response.json); it finished after this entry was first written: is_error=false, 63 turns, 3795 s, modelUsage antigravity/gemini-3.8-flash only (input 756372, output 39375, cache read 3116607), verdict GPU_COMPOSITION_SUSTAINED. Every fact above was extracted by the main agent directly from boot.log; the Gemini report was then checked against the log: its gate lines (813/816/820/3381), three import lines, 28-row frame table, 0011 comparison lines (792/1578/2706/2708/2713/3076/3138/3139), process lines (542/642/1630/1857; exactly one exec of backboardd and of SpringBoard; CRASH lines only for bluetoothd, fairplayd.H2, assetsd, mediaserverd, driverkitd, same class as before; the three "panic" matches are the benign gPanicBase/DumpPanic boot lines also present in the 0011 log) and monitor log all match. NOT accepted from that report: its section 9.5 explanation that the late frames are a "physical limit" obtained by adding averages of unrelated counters, and its source citation BackboardGate.hpp:98 (the copy read by the main agent has the test at line 104). New kernel-side timing lines it surfaced, verified verbatim, are leads for the rate question, semantics not yet read in source: boot.log 4542 "FUXI GPU: presents GPU 137 CPU 459, GPU retire avg 15870 us CPU avg 1703 us ... wait 3659" (0011 boot line 5128: "presents GPU 571 CPU 22, GPU retire avg 15980 us ... wait 15489"); 4544 "FUXI GPU ABI: calls n/avg/max us ... submit 570/8393.0/31927 wait 2075/5216.4/58069 ... import 217/152.7/2235" (0011 line 5130: submit 31/1457.2/14599, wait 178/649.8/5966); 4545 "sleep 83427/134.4/1555"; 4546 "submits 570 ibs 13392 max 123 over16 257"; 618 "FUXI KMD: submit IBs" lines, last fence 618. Arithmetic only (not a conclusion): 570 submits x 8.39 ms + 2075 waits x 5.22 ms is about 15.6 s over 613 frames, about 25 ms per frame spent inside submit/wait calls, which is the same order as the observed 16-21 Hz; whether those calls block the frame path, and why a submit averages 8.4 ms, is the first thing to read (kernel GPU ABI submit/wait and the sleep-polling stage), together with FUXI_MTL_LATE_FRAMES detail lines. harness docs/ASTC_LAYOUT.md "Validation" still says no device was available; update it with the next commit.

2026-11-13 follow-up, ANALYSIS ONLY (user: "好，接著查"): why backboardd GPU composition runs at only about 16-21 Hz after 0012. No device access, no boot, no image write and no code change in this step.

Meaning of the counters (source-checked): gpu_ms and late come from the NativeMetal completion hook (D:/xiaomi13/fuxi_ios/ios-fuxi/tools/patches/nativemetal-fuxi-0002-safety-hooks.patch:1019-1023): gpu = command-buffer GPU end minus start, wall = commit to completion; BackboardGate.hpp:98 counts a frame late when wall > 16.67 ms. gpu_busy_est_pct is gpu_ms x hz (28.540 x 15.74 = 44.9). So the GPU is NOT saturated: a 48-64 ms frame period contains only 13-29 ms of GPU time; the rest is spent outside GPU execution.

Log facts, native-layout0012-survey-boot-1/boot.log: line 136/139 "FUXI GPU CLOCK: level=0 gpu_index=1 requested_khz=220000 ... bw_index=0 ddr_kBps=0" (lowest GPU clock, DDR bandwidth vote off; shim/fuxi-probe/gpu-clock.h levels 1..8 = 220/295/348/401/475/550/615/680 MHz, DDR vote 0 at level 0, 2136718 kB/s at 1, 6074218 at >=2). Line 287: the loader args carry fuxigpuncimport=1024 only (bit 10 read-only import, picked on 2026-11-09 as a bounded diagnostic, see the roimport entry above), no fuxigpulevel, and no fuxiperf=1 although the upstream base command line has it (kernel-source-979/tools/ci/kernel-inputs.py:26-35, kernel-surface-artifact/cmdline.txt); the monitor was run without --cpu-level, so the CPU clusters sit at whatever ABL left (thermal.md:340-342) and the log has no FUXI PERF line or temperature data. Lines 4652-4658 submit_split per 600 submits: total 8363.8 us/submit = retry_sleep 4913.6 (29.5 attempts; refusals busy 825, noq 16304) + backend 3410.3 (of which cache 2334.8: 35694 KB rw + 30198 KB ro cleaned per submit, 100 imports, 66 MB mapped) + small rest. Line 4544: wait 2075 calls avg 5216 us; line 4545: 83427 sleeps avg 134 us. Line 4542: presents GPU 137 CPU 459 (0011 boot line 5128: GPU 571 CPU 22), i.e. with a GPU client active the display copy mostly falls back to the CPU path. Line 978: cached-coherent BOs disabled reason=bit29-off.

Mechanisms (xnu/fuxi-adreno/fuxi_gpu_abi_backend.inc; the device kernel source kernel-source-979 equals D:/xiaomi13/fuxi_ios/ios-fuxi modulo CR except fuxi_gpu_scanout.inc): lines 1037-1040, without queue mode (fuxigpuncimport bit 9, value 512) a submit is refused with reason 2 ("noq") whenever the GPU is not idle, and Turnip sleeps and retries: that is the 4.9 ms retry_sleep. Lines 220-257, every submit cleans ALL mapped imports and retirement invalidates the GPU-writable ones unless (a) bit 6 (64) coherent-import self-test passed (lines 1227-1251: imports then map write-back inner-shareable and all import cache maintenance is skipped), (b) bit 19 (524288) dirty tracking cleans only pages whose pmap modify bit is set (lines 165-219), or (c) bit 0, an unsafe no-CPU-write contract. Bit 26 (67108864) replaces fence polling by a GPU interrupt; unixnu-port.md:564-567 says not yet observed on a device. fuxibbgpu=2 only adds work after a refusal (metal-m9l.md:116), so it is not a per-frame cost. Full bit list parsed at line 582: 0 ncimport, 1 fenced WPTR, 2 stall recovery, 6 coherent self-test, 7 ranged TLBI, 8 mapwait, 9 queue, 10 roimport, 11 pace, 12 hold, 13 start, 14 probe, 15 spin, 16 mid, 17 autorefresh, 18 hard spin, 19 dirty, 20 scanout pre-clean, 21 inline (needs 9), 22-24 DPU scanout stages, 25 PAC self-test, 26 fence IRQ, 27 copy park, 28 not-shown log, 29 cached-coherent BOs (needs 6); 3/4/5 are frame log, copy priority, superseded skip.

What upstream ran on its device (docs, not reproduced here): gpu-scanout.md:1424 O220 log with fuxigpuncimport=2948672 (bits 6,9,10,11,12,13,14,15,18,19,21) reports the panel at about 84 Hz with about 1.75 client swaps per shown frame; :1509 Q222/R223 used 2948680/2948728; :2077 names 472710776 as the then-current base. gpu-clock.md: "supplied device observation at level 6 is 7-11 ms GPU time for about 100 draws on a 1080x2400 surface"; recommended order level 1, then 2 and 4, then 5/6 if stable and temperature acceptable; no GPU thermal governor and no device/thermal validation claimed. gpu-fps.md:5-9: 4.9-11.3 ms median GPU execution, 17-25 ms median wall with two flights. This device therefore runs an untuned baseline of the same stack.

Inference, NOT measured: the frame is a serial chain commit -> kernel submit (wait for an idle GPU, then clean 66 MB) -> GPU execution at 220 MHz without a DDR vote -> polled fence wait and invalidate -> present. Arithmetic only: 570 x 8.39 ms + 2075 x 5.22 ms = 15.6 s over 613 frames = about 25 ms per frame of kernel submit+wait time. Which term dominates is unproven until A/B boots.

Proposed boot-arg-only A/B (NOT started, needs user approval): build loader variants the same way as prepare-nocopy-touch-loader.py (only bytes 72..680 of the payload change; current args 445 of 607 characters), fastboot boot only, one monitor with --thermal-log. Variant 1: add fuxigpulevel=4 (401 MHz, DDR 6074218 kB/s). Variant 2: fuxigpuncimport=1600 (1024 + 512 queue + 64 coherent self-test), optionally + 524288 dirty tracking as the fallback if the self-test refuses. Stop conditions as in gpu-clock.md (HFI error/0x90a, timeout, fault, hang, corruption, missing fence, panic, abnormal temperature).

## 2026-11-13 experiment 1: fuxigpulevel=4 (401 MHz), ONE bounded temporary boot

User approved ("試 401 MHz"). Nothing flashed, no image write, no code change.

- Loader: `E:\Codex-Xiaomi13-GPU-20261109\native-gpulevel4-loader\` built by `prepare-gpulevel4-loader.py`
  (source = `native-nocopy-touch-survey-loader\payload.bin`, boot-args + ` fuxigpulevel=4`, 460 chars; every byte
  outside payload 72..680 identical to the 220 MHz loader, asserted on payload and on Image).
  payload sha256 `9dcc6d26ea2dfc1c426ef7dc8aa4bc8ce626471a24193ecc09a61a0719b04305`,
  boot.img sha256 `1cef20961498fdfc157a22fbbba36279a43a96e4b710da8cc72a944689da6b0a`.
- Pre-boot: map -> `verify-native-map.py` OK (inode 199819, 39 extents) -> `verify-gpulevel4-payload.py` OK -> unmap,
  `mapped=false`. Battery 100 %, 30.3 C.
- Boot: `fastboot boot`, single monitor, `--seconds 240 --reset-minutes 4 --skip-clocks --touch-power --thermal-log 10`.
  Evidence: `native-gpulevel4-boot-1\` (boot.log 6513 lines, thermal.jsonl 24 samples), `native-gpulevel4-monitor-1.log`.
  Afterwards Android `sys.boot_completed=1`, `mapped=false`, no monitor process.
- Kernel accepted it (boot.log:136,139): `level=4 gpu_index=4 requested_khz=401000 gx_arc=0x400204 bw_index=2
  ddr_kBps=6074218 hfi_ack=1`; "requested 401000 kHz GPU vote (not measured)". No HFI error, no 0x90a, no GPU fault,
  fence timeouts 0, failures=0.

### NOT a clean A/B: the user was touching the screen
29 real contacts (`FUXI TOUCH: contact N`) are interleaved with the scripted touches; the 220 MHz baseline had script
touches only. Numbers below are indicative, not a controlled comparison.

| metric | 220 MHz (layout0012-survey-boot-1) | 401 MHz (gpulevel4-boot-1) |
|---|---|---|
| gpu_ms per window | 10.3 / 15.0 / 30.0 / 13.5 / 28.5 | 9.6 / 18.2 / 8.9 / 12.0 / 12.5 |
| hz per active window | 20.2 / 6.9 / 20.9 / 15.7 | 24.7 / 13.9 / 7.6 / 16.2 / 16.4 |
| late / frames (wall > 16.7 ms) | 509 / 613 = 83 % | 491 / 728 = 67 % |
| submit_split total us/submit (first 600) | 8363.8 | 5988.3 |
| - backend | 3410.3 | 2814.4 |
| - retry_sleep | 4913.6 | 3160.3 |
| - attempts per submit | 29.5 | 6.3 |
| refusals busy / noq | 825 / 16304 | 1560 / 1667 |
| cache clean us/ok | 2334.8 | 2476.5 |
| fence pending histogram, modal bucket | 16-32 ms | 8-16 ms (first 600), 16-32 ms (second 600) |
| presents GPU / CPU | 137 / 459 | 205 / 392 |

Reading: the higher clock cut GPU time and the idle-wait retries, but the composite rate stayed in the same
16-25 Hz band. GPU clock is therefore not the main limiter; per-submit cache cleaning (unchanged ~2.4 ms) and the
sleep-polled submit/fence path remain. (Inference from the table above; not a controlled measurement.)

### backboardd crashed once in this run (new; cause NOT established)
boot.log:5496-5499: `CRASH backboardd[56] sig 11 code 0x100000001 ... far 0x0`, pc 0x10ebecfb4 lr 0x10ebecfa0,
bt 0x10ebec650 0x10d7ff498 0x10d5b4fd4 0x10d5b67e8 0x10d400938 0x10ad1f860 0x1c5160754 0x1c5155d90 0x1c5161658
0x1c51a6d00. Sequence: real tap at 106,573 (5466) -> SpringBoard activity -> backboardd creates surfaces 62/63
(5483-5484, 1125x2436) -> submits of 37..68 IBs -> crash after fence 987 (frames >= 728). launchd restarted
backboardd and SpringBoard (5502, 5511); gate re-allowed GPU composition (5573), 52 frames by the end.
No `CRASH backboardd ... sig 11` exists in any other boot.log under `E:\Codex-Xiaomi13-GPU-20261109`. The baseline
never had a manual tap / app launch, so this cannot be attributed to 401 MHz vs. the app-launch path yet. Not
symbolicated (no image load addresses in the log; low-14-bit matching of return addresses against the bundle/Turnip
binaries is the next step).

### Thermal + CPU (first data for this configuration)
- TSENS max: CPU 63.1 C at +50 s (boot burst), GPU sensors max 48.1 C at +190 s, 46-48 C at the end (+230 s).
- EPSS votes are at the top rows, not low: silver 1670.4, gold vote 2803.2, prime vote 3187.2 MHz (as left by ABL;
  no `fuxiperf=1`). `state_mhz` read 499.2 / 595.2 in 17 of 23 samples and 2707.2 / 2956.8 in the busy ones. Why the
  state register reads the lowest row most of the time is not established (idle clusters is my guess). The earlier
  hypothesis "CPU stuck at a low ABL level" is not supported by the votes.

### Still open
- Experiment 2 (`fuxigpuncimport=1600`, queue mode) not run, not yet approved.
- A clean A/B needs a run without manual touches (or both runs with the same touches).

## 2026-11-13 experiment 2: fuxigpuncimport=1536 (1024 + bit 9 queue), 220 MHz, ONE bounded temporary boot

User approved ("試 queue 模式"). Used 1536, not the earlier-proposed 1600: 1600 also sets bit 6 (coherence self-test,
which can disable all import cache maintenance) = a second variable. Nothing flashed.
- Loader `native-queue1536-loader\` (`prepare-queue1536-loader.py`; Image differs from the baseline loader in 3 bytes).
  payload sha256 `cfbd1231b2578101c3873bb0ba73f5c3828afe94788d2ce3e13948c60ebd0567`,
  boot.img sha256 `8425d59b2a8cb28fa684f8a283e36097d2115e249f5f5381ee3bcef64679df23`.
- Pre-boot map/verify OK (`verify-queue1536-payload.py`), `mapped=false`. Evidence `native-queue1536-boot-1\`
  (boot.log 6552 lines), `native-queue1536-monitor-1.log`. Afterwards Android `boot_completed=1`, `mapped=false`.
- Clock line: level=0 220000 kHz. No FUXI GPU HANG / fail / HFI error, failures=0. Max temp 61.5 C CPU (+200 s), GPU <= 50.1 C.
- Queue active: boot.log:3897 `queue queued 93 deepest 4 refused present 8922 full 0`; :5907 `queued 193 ... refused present 15660`.
- submit_split first 600 (3843-3844): total 6291.2 us, backend 3143.5, retry_sleep 3127.2, attempts 16.1,
  refusals busy/hold/noq/pw/full 771/0/0/8316/0, cache 2453.4. So "noq" is gone but the refusals moved to
  "present waits" (reason 3), which bit 21 (inline, needs bit 9) is documented to remove.
- hz per window: 13.0 / 12.3 / 23.1 / 41.95 / 6.0 / 19.2, then after a counter reset 25.5 / 20.0 / 10.5.
  `SWEET FB: stats 600 frames in 77169 ms` (baseline 212724 ms) - NOT comparable: 14 real touch contacts again.
- Frame counter reset 887 -> 129 and 4 log lines match `CRASH (backboardd|SpringBoard)`: backboardd restarted again,
  now at 220 MHz. NOT YET READ in detail (usage limit hit). If it is the same signature, 401 MHz is not the cause.

### Crash of experiment 1 symbolicated (low-14-bit matching, scratchpad match_bt.py)
libnative_metal_bridge.dylib slide 0x10d3e0000, all 4 return addresses are call sites:
`nmb_render_encoder_new+0xaec` -> `CommandBuffer::render_command_encoder+0x1c` -> `CommandBuffer::open_render+0x888`
-> `EncoderCore::open+0x174` -> libvulkan_freedreno.dylib (slide 0x10eb9c000, 2 matches, coherent chain):
`tu_BeginCommandBuffer+0x1d8` -> pc in `tu_init_hw<A7XX>+0xcc`, far 0x0 (NULL write). Guess, unverified: command-stream
BO allocation failed and Turnip wrote through a NULL cs pointer.

### Not done
- Read the experiment 2 crash lines and compare signature; disassemble tu_init_hw+0xcc.
- Verify the Gemini report `perf-0012\gemini-gpu-knobs.md` (task finished, output NOT reviewed); close terminal tab c2.
- Clean A/B without manual touches; next candidate knob: bit 21 inline (1536+2097152) - not approved.

## 2026-11-13 user report on experiment 2: "screen flickers all the time while swiping" - source analysis only

Analysis was done with no shell (auto-mode classifier down): no adb, no boot, no Gemini delegation at that point.
Everything in this section is read from source and existing logs.

Facts (file:line):
- Experiment 2 boot args (boot.log:289) have no `fuxigpuscanout`. `SweetFramebuffer.cpp:1061` only calls
  `sweet_surface_present_begin` (the "source still has a GPU writer" gate) when `fScanout` is set. All three 0012 runs
  print `swap_ready_us=0/0` (survey 4536, gpulevel4 4647, queue 3887): a swap is never held for readiness.
- `fuxi_adreno.c:889-891`: a present that finds a client job running sets `fg_abi_present_wait` and returns 0;
  `SweetFramebuffer::present` then CPU-copies the IOSurface at once. Presents GPU/CPU: survey 137/459,
  gpulevel4 205/392, queue 116/481 and 161/435; `cache hit 592 miss 3`, so slot/generation is not the reason.
  `refused present 8922/15660` (set only at fuxi_adreno.c:890 and fuxi_gpu_scanout.inc:2350-2351) shows presents
  arriving while client jobs run.
- NativeMetal `commitAndWaitUntilSubmitted` = commit + `waitUntilScheduled` (`native\NativeBridgeMetalFrontend.mm:865`,
  1030-1038). Runtime: `Scheduled` = last batch submitted to Vulkan, not completed
  (`m2v-lf\crates\metal2vulkan-runtime\src\command_buffer\mod.rs:12-19`, 1864, 1957-1972).
- NativeMetal never takes the IOSurface writer lease: no `IOSurfaceLock`/`IOSurfaceUnlock` in the frontend or in
  `harness\patches`; `sweet_gpu_writer` is only reached from `trapLock`/`trapUnlock` (SweetSurface.cpp:1384-1416).
  Upstream FuxiMetal does (`tools/fuxi-metal/Frontend.mm` SurfaceLease), and its docs state the requirement:
  docs-lanes/metal.md:2109-2199 (CA calls `MetalContext::flush(true)` right before the IOMFB swap; swap must not copy
  an incomplete surface), docs-lanes/gpu-scanout.md:1427-1429, 1471-1474.
- Queue mode keeps up to 4 client jobs in the ring (`deepest 4`); fence waits avg 14.6 ms max 47 ms (3877),
  swap -> present avg 13.8 ms (3887).

Inference (NOT measured on device): the display copies a swap surface before its render job finished - in queue mode
often before it started - so the panel shows that surface's previous content (3 swaps old) or a partial frame. Invisible
on a static screen, a back-jump/flicker while scrolling. The same race exists at 1024 (same CPU-copy share, same
`swap_ready_us=0/0`) but there only one job is in flight, so the window is narrower. Kernel queue-mode retire/ivac
bookkeeping (fuxi_gpu_abi_backend.inc:220-257, 533-567, 1272) was read and looks consistent; presents are serialised
in `presentStep`, so GPU-vs-CPU copy reordering is ruled out.

Fix candidates:
- A (boot arg only): back to `fuxigpuncimport=1024`. Mitigation + A/B confirmation; latent race stays.
- B (frontend patch 0013, preferred): in `commitAndWaitUntilSubmitted`, when `encodedWork.storedSurface`, wait for
  completion (`nmb_command_buffer_wait(_handle,0)`, which returns after the kernel retire + ivac) before returning.
  Costs pipelining on the render thread (about one GPU job time per frame). Needs commit -> iOS CI -> light deploy.
- C: writer lease in NativeMetal (`IOSurfaceLock(kIOSurfaceLockAvoidSync)` at commit, unlock at completion) plus
  `fuxigpuscanout=1`. Upstream design, keeps pipelining, but turns on the untested guarded-present/scanout path.
- D (boot arg only, speculative): bit 21 inline (2098688): the present copy queues in the ring behind the client job,
  so ring order would give the ordering. Never run with NativeMetal; falls back to the racy CPU copy whenever
  `fg_inl_usable()` is false.

Experiment 2 crash (boot.log:4752-4755): backboardd sig 11 far 0x0, pc/lr/first bt `0x10e9ecfb4 / 0x10e9ecfa0 /
0x10e9ec650` - same low bits as experiment 1 (`...ecfb4 / ...ecfa0 / ...ec650`, `tu_init_hw<A7XX>+0xcc`), at 220 MHz.
So 401 MHz is not the cause. Rest of the chain not symbolicated yet.

`perf-0012\gemini-gpu-knobs.md` was read: NOT accepted as verified (blanket "validated" statuses, some bit
descriptions unchecked against source). Do not quote its "recommended" arg set as tested.

## 2026-11-13 patch 0013 (swap wait = fix candidate B): implemented, CI green, staged

Facts below come from the files named; statements marked (prev. session) were not re-checked in the session that wrote
this section.

- Patch `xiaomi13-layout-backport-20261112\harness\patches\nativemetal-fuxi-0013-swap-wait.patch`:
  `commitAndWaitUntilSubmitted` = commit + `waitUntilScheduled` + new `fuxiWaitStoredSurface`. When the command buffer
  stored an IOSurface (`encodedWork.storedSurface`) it calls `nmb_command_buffer_wait(_handle,0)` before returning.
  Default ON. Off with env `FUXI_NATIVE_SWAP_WAIT=0` or boot arg `fuxiswapwait=0` (env wins).
  Log line on the first wait and every 600: `FUXI MTL: native swap wait n=... avg_us=... max_us=... refused=...`.
  Doc: `harness\docs\SWAP_WAIT.md`. Host tests cover token parsing, env gating and the no-op path only.
- Implemented by Gemini (`antigravity/gemini-3.8-flash`); diff reviewed and the patch stack replayed from the baseline
  byte-identical to the work tree (prev. session).
- Commit `17fbbec5ed5a9939070cf73903655c418a85859a` on `codex/astc-layout-backport`, in sync with origin.
  CI: iOS run 43353856490 and host tests 43353859965 passed (prev. session). Artifact 14005971356, zip sha256
  `55c9a504013b2f7814d4e4f69b638992f8aac32d8cb2d77bccd38e819d60a5f0`, checked by `verify-ci-artifact-0013.py` ->
  `artifact-verification-0013.json` (FuxiNativeMetal `413439f7...`, libnative_metal_bridge `5e304a3d...`).
- Staging: `native-layout0013-overlay.qcow2`, backing file `native-layout0012-root.img` (the clean 0012 host image),
  `prepare-layout0013-guest.py`; `native-layout0013-stage.log` ends `INSPECT_SUCCESS`, fsck OK three times, staged
  bundle hashes equal the artifact hashes above. Export `native-layout0013-root.img`, 21474836480 bytes, sha256
  `b5c10b143553698330a54d01249a7f34cc9f8a96a818760b8819ac340626fb85`.

## 2026-11-13 patch 0013 deployed and booted once: user reports NO flicker; smoothness still lacking

User approved ("已切換，繼續" on the plan: light deploy + one bounded queue-mode boot). Light flow, no new full backup;
restore point = commit `17fbbec` + clean host images `native-layout0012-root.img` / `native-layout0013-root.img`.

### Deploy (all under `E:\Codex-Xiaomi13-GPU-20261109\`)
- Transfer `delta-stage-hash.py` -> `native-layout0013-transfer.log`: 287 differing 4 MiB blocks (1148 MiB), delta sha256
  `1f6caab4...`, `STAGE_VERIFIED /data/local/tmp/codex-layout0013-stage.img b5c10b14...`, `transfer_exit=0`.
- Guarded write: `native-layout0013-write.sh` (sha256 `bbafa77e...`) run by `native-layout0013-supervisor.sh`
  (sha256 `bc9dad4e...`), control dir `/data/local/tmp/codex-layout0013-write-control`. Device sha256 before the write
  `08f89ad4ed7ba457ae9544c54091f3f94068da5762f9d88717c7a562ba58fbfc` (= 0012), status 0.
- Readback after unmap/remap: mapper sha256 = `b5c10b14...` (READBACK_MATCH).
- Pre-boot: `verify-native-map.py` OK, `verify-queue1536-payload.py` OK, unmap, `mapped=false`.

### Boot (11:17:08, nothing flashed)
Loader = the unchanged experiment 2 loader `native-queue1536-loader\boot.img` (sha256 `8425d59b...`), so the only
difference to experiment 2 is the root image (0012 -> 0013). Monitor as before (240 s, reset 4 min, thermal log 10 s).
Evidence `native-layout0013-queue1536-boot-1\` (boot.log 9986 lines, sha256 `aeb70264...`),
`native-layout0013-queue1536-monitor-1.log`. Afterwards Android `boot_completed=1`, `mapped=false`, no monitor process.
Evidence report `native-layout0013-queue1536-boot-1-evidence.md` (extracted by Gemini 3.8 read-only, 58 tool calls;
its header lists which items the main agent re-derived independently and which it did not). Boot args identical to
experiment 2 (boot.log:287 vs :289).
The monitor log ends with the same `USBError: Entity not found` traceback + `monitor_exit=0` as experiment 2 (device reset).

### Result
- **User (eyes on device, swiping): "無閃爍了 流暢有待提升"** = flicker gone, smoothness needs work. This is the only
  evidence for the flicker; nothing in the log measures it.
- Swap wait active, never refused: boot.log:1574 `native swap wait n=1 avg_us=3663`; :4586 n=600 avg 14565 max 47201;
  :5939 n=1200 avg 14544 max 79001; :7299 n=1800 avg 12480; :8798 n=2400 avg 13693; `refused=0` on all five.
- No `CRASH backboardd` / `CRASH SpringBoard` in this run (experiment 2 had one at boot.log:4752, `...ecfb4`). One run
  only: says nothing about whether the `tu_init_hw` NULL write is gone. No `FUXI GPU HANG`, `FUXI GPU: fail`, `0x90a`,
  `fence timeout`. Other CRASH lines are the usual fairplayd.H2 / bluetoothd / driverkitd / assetsd / mediaserverd.
- Clock unchanged: boot.log:136 level=0 220000 kHz. `gate allow backboardd` at :818.

NOT a controlled comparison: 97 `FUXI TOUCH: contact` lines here vs 14 in experiment 2 (user swiped much more).

| metric | 0012 queue (native-queue1536-boot-1) | 0013 queue (this run) |
|---|---|---|
| `SWEET FB: stats` 600 frames in ms | 77169 / 128372 | 99620 / 32300 / 20568 / 55103 |
| -> hz (600000 / ms; includes idle time) | 7.8 / 4.7 | 6.0 / 18.6 / 29.2 / 10.9 |
| copy avg us | 4668 / 5889 | 12371 / 11574 / 10716 / 11820 |
| presents GPU / CPU | 116/481, 161/435 | 426/171, 392/208, 350/250, 400/200 |
| queue `refused present` (cumulative) | 8922, 15660 | 483, 483, 483 (:6026, :7398, :8898) |
| queue `deepest` | 4 | 2 |
| submit_split first 600: total / backend / retry_sleep us, attempts | 6291.2 / 3143.5 / 3127.2, 16.1 | 6938.3 / 2539.7 / 4389.5, 5.0 |
| refusals busy/hold/noq/pw/full per 600 | 771/0/0/8316/0, 784/0/0/7764/0 | 2423/0/0/0/0, 2234/0/0/483/0, 3186/0/0/0/0, 2478/0/0/0/0 |
| fence waits per 600 avg us | 14556 / 15796 | 14049 / 13525 / 7909 / 16952 |
| `SWEET FB: stages` swap_ready_us | 0/0 | 0/0 (no `fuxigpuscanout`, as expected) |
| stages submit_fence_us avg | 3078 / 4284 | 11343 / 10430 / 9300 / 10649 |
| `FUXI KMD: ... fence` lines (monotonic, no gaps in both) | 1385 | 2851 |
| GPU retire avg us | 15928 / 15971 | 15982 / 15971 / 15949 / 15980 |

Thermal (monitor log `thermal +`, 24 samples): max 67.8 C cpu-1-5 at +80 s; GPU sensors max 52.9 C (+110 s);
last sample +230 s 59.9 C cpu-1-8, GPU 52.1 C.

Reading (inference, not measured): with the wait in place a present almost never finds a client job running
(`refused present` 483 instead of 8922+), so most presents take the GPU path (58-71 % instead of 19-27 %) and no swap
is copied before its render finished - consistent with the flicker being gone. The price is the predicted loss of
pipelining: per shown frame the render thread now waits ~12.5-14.5 ms for its own GPU job and the present blit takes
another ~9-11 ms submit->fence, both at 220 MHz. Best window 29.2 Hz; that serial chain is the smoothness ceiling now.
Refusals moved from "pw" (present wait, 8316 -> 0) to "busy" (771 -> ~2400 per 600 submits) and retry_sleep rose to
4.2-5.8 ms per submit: client submits now sleep-poll behind a running GPU job (which job - my guess is the present
blit - is not established).

### Next candidates for smoothness (none approved, none run)
- Boot arg only: `fuxigpulevel=4` (401 MHz) on top of 1536 + 0013. Experiment 1 showed lower GPU time at 401 MHz with no
  HFI error; with the swap wait the GPU job time is now directly in every frame, so the clock should matter more than
  it did there. Needs a new loader variant (same method as `prepare-queue1536-loader.py`) and one more approved boot.
- Per-submit cache clean (~2.4 ms): bit 19 dirty tracking / bit 6 coherent self-test. Fence polling: bit 26 IRQ (never
  observed on a device).
- Fix candidate C (writer lease + `fuxigpuscanout=1`) would give ordering without giving up pipelining; larger change.

## 2026-11-13 experiment 3: patch 0013 + queue 1536 + fuxigpulevel=4 (401 MHz), ONE bounded temporary boot

User approved ("試 401 MHz"). Boot arg only: nothing flashed, no image write (root image stays 0013, `b5c10b14...`).

- Loader `native-queue1536-gpulevel4-loader\` built by `prepare-queue1536-gpulevel4-loader.py`: source =
  `native-queue1536-loader\payload.bin` (`cfbd1231...`), boot-args + ` fuxigpulevel=4` (460 chars). Asserted in the
  script: payload identical outside bytes 72..680; Image identical to the queue1536 Image except 15 bytes.
  payload sha256 `788caa2fcae12fe90238e00da64502a819865240eaff13103090d5736dde08a7`,
  boot.img sha256 `191bba9b274ec1acb93605db852f188d3fa56aa7c9eb0cd3946d9b7ba31df7e4`.
- Pre-boot: map -> `verify-native-map.py` OK (inode 199819, 39 extents) -> `verify-queue1536-gpulevel4-payload.py` OK
  -> unmap, `mapped=false`. Battery 100 %, 33.7 C.
- Boot 11:40:09, same monitor arguments as before. Evidence `native-layout0013-queue1536-gpulevel4-boot-1\`,
  `native-layout0013-queue1536-gpulevel4-monitor-1.log`.
- Kernel accepted it (boot.log:136,139): `level=4 gpu_index=4 requested_khz=401000 gx_arc=0x400204 bw_index=2
  ddr_kBps=6074218 hfi_ack=1`; "requested 401000 kHz GPU vote (not measured)".
- Afterwards Android `boot_completed=1` (11:44:55), `mapped=false`, no monitor process. boot.log 10348 lines, sha256
  `c8607731...`. Numbers below were grepped by the main agent directly (no separate Gemini evidence report for this run).

### Result
- **User: "我沒有感覺變流暢 目標是120hz"** = no perceptible gain from 401 MHz; the target is 120 Hz.
- No `FUXI GPU HANG` / `FUXI GPU: fail` / `HFI error` / `0x90a` / `fence timeout` / `GPU fault`; no
  `CRASH backboardd` / `CRASH SpringBoard`; 2444 fence lines, monotonic. `gate allow backboardd` at :800.
- New: `CRASH Preferences[285] sig 11 ... pc 0x0 lr 0x1e8b2458c far 0x0` (boot.log:5593-5596) - the Settings app died on
  a NULL call. Cause not established; first time an app other than the usual daemons is in the CRASH list.
- Thermal (24 samples): max 63.1 C cpu-1-8 at +180 s, GPU sensors max 55.3 C (same sample), last +230 s 47.7 C.

99 `FUXI TOUCH: contact` lines (220 MHz run: 97). Similar amount of swiping, still not a controlled comparison.

| metric | 0013 queue 220 MHz | 0013 queue 401 MHz (this run) |
|---|---|---|
| native swap wait avg us (n=600/1200/1800/2400) | 14565 / 14544 / 12480 / 13693 | 10178 / 10876 / 10457 / 11275 |
| fence waits per 600 avg us | 14049 / 13525 / 7909 / 16952 | 9587 / 10979 / 9639 |
| `SWEET FB: stats` 600 frames in ms | 99620 / 32300 / 20568 / 55103 | 106853 / 37167 / 39267 / 30834 |
| -> hz (includes idle time) | 6.0 / 18.6 / 29.2 / 10.9 | 5.6 / 16.1 / 15.3 / 19.5 |
| copy avg us | 12371 / 11574 / 10716 / 11820 | 11980 / 12202 / 12239 / 13338 |
| stages submit_fence_us avg (present blit) | 11343 / 10430 / 9300 / 10649 | 10688 / 10789 / 11108 / 12141 |
| GPU retire avg us | 15982 / 15971 / 15949 / 15980 | 15997 / 15990 / 15987 / 15909 |
| presents GPU / CPU | 426/171, 392/208, 350/250, 400/200 | 401/195, 405/195, 417/183, 458/142 |
| submit_split total / retry_sleep us, attempts | 6938.3 / 4389.5, 5.0 (first) | 7190.3 / 4346.1, 5.0 (first); 6595-7936 / 3725-4451 |
| refusals busy / pw per 600 | 2423/0, 2234/483, 3186/0, 2478/0 | 2410/0, 2444/176, 2071/60, 2442/0 |
| cache clean us/ok | 2483 / 3146 / 2806 / 3032 | 2778 / 3287 / 2790 / 3468 |

Reading:
- The clock helped only the client render job (swap wait 13-14.5 -> 10-11 ms). Everything on the present side is
  unchanged: blit submit->fence ~10.7 ms, GPU retire ~16.0 ms. So the present path is not GPU-clock bound.
- boot.log:76 `SWEET FB: present up to 60 Hz (every 1 ticks, tick 16666979 ns)` and `callbacks_vsync` ~60/s
  (1850 in 30.8 s): the framebuffer present is paced by a 16.67 ms tick. **60 Hz is a hard cap of the current display
  path**, independent of the GPU. `GPU retire avg` = 16.0 ms in every run of every configuration matches that tick.
- New log line in this kernel: `submit_split waits/submit by kind: copy 4.0 (1080 us each)` - a client submit waits
  ~4 times ~1.1 ms on "copy" (the display copy job, per the name; code not read yet). That is the "busy" refusal /
  retry_sleep ~4.3 ms per submit.
- Per active frame the chain is still serial: render job ~10.5 ms (incl. ~2.8 ms cache clean and ~4.3 ms waiting behind
  the copy) + present ~10.7 ms blit, retired on the 16.67 ms tick -> ~30 Hz ceiling. 120 Hz = 8.33 ms per frame.

### Goal stated by the user: 120 Hz
Not reachable by boot-arg tuning of this path. Open questions handed to Gemini (read-only source/doc survey, task file
`gemini-120hz-task.md` in the session scratchpad; report to be saved as `perf-0013\gemini-120hz-survey.md`): where the
60 Hz tick comes from and what switches it, the panel's own refresh rate under XNU, what a present does per frame and
whether a no-copy DPU scanout path exists, what upstream measured above 60 Hz and with which prerequisites.

## 2026-11-13 120 Hz survey: Gemini report received, checked against source

Gemini 3.8 finished (71 turns, 70 tool calls, read-only). Report saved as
`E:\Codex-Xiaomi13-GPU-20261109\perf-0013\gemini-120hz-survey.md` (raw `result` field of the response JSON).
The check below was done in a session whose shell was blocked; nothing was booted, built or written for it.

Re-derived by the main agent from `kernel-source-979` (file:line read directly):
- 60 Hz tick: `SweetFramebuffer.cpp:606-623`. `hz` defaults to 60, `sweetfb_hz` overrides; `fTickAbs` = 16667 us unless
  `hz > 60` (then 1000000/hz us). The log text is `60 / fFlushTicks`, so it prints "60 Hz" even with `sweetfb_hz=120`.
- Without `fuxigpuscanout=1` the kernel never touches DPU/DSI: the panel stays in ABL's command-mode 60 Hz with
  autorefresh (`fuxi_gpu_scanout.inc:8-11`, 1805-1851). So a faster tick alone cannot show more than 60 Hz.
- Takeover: `fScanout = scanout == 1 && quiet == 1` (`SweetFramebuffer.cpp:651-661`). `sweetfb_noconsole=1` is already in
  our boot args (boot.log:289). Ownership clears INTF1 AUTOREFRESH bit31 and from then on every frame needs a software
  START (`fuxi_gpu_scanout.inc:1809-1851`).
- Panel 120 Hz: `fuxipanel120=1` -> `fg_panel120_switch` (`fuxi_gpu_scanout.inc:1706-1796`), called once from the acquire
  path after the inherited transfer drained (:1913). 8 DCS packets over DSI0 + tear-check count halved
  (0x180085 -> 0x180042, period 8333 us). Refuses before sending anything if the DSI controller is not in the inherited
  command state (:1729-1734, "keeping 60 Hz"); a timeout mid-sequence stops scanout for the boot ("panel state
  uncertain, reboot required", :1791-1795). `IOMFBDisplayRefresh.displayMinRefreshInterval` (1/120 s) is published only
  with `fuxipanel120=1` and no `sweetfb_hz<=60` (`SweetFramebuffer.cpp:408-456`).
- TE-locked vsync: `fuxivsyncte=1`, valid only while `fScanout` (`SweetFramebuffer.cpp:458-474`).
- Double/triple buffers: `fuxiscandouble=1 fuxiscantriple=1` (`fuxi_gpu_scanout.inc:138-140`); B/C at 0xb8a00000 /
  0xb9400000 (:1936-1950). Stage 3 no-copy needs triple + `fuxigpuncimport` bits 22+23+24 and no bit 17 (:1223-1238);
  eligibility is one layer, BGRA8, wired IOBuffer, 1125x2436 stride 4608 crop 22,18 1080x2400, page-aligned offset
  (`SweetSurface.cpp:473-481`). Source comment: "Not yet observed on device"; docs gpu-scanout.md:2071-2075 say the same.
- Bit 17 autorefresh scanout: source says DO NOT ENABLE, "can never reach 120 Hz" (:1413-1415).

Not re-checked by the main agent (Gemini's statements only): section C.2 log-field definitions, section E bit table
except bits 17, 22-24, the gpu-fps.md summary in D.2.

Correction to the Gemini report (section F): it says a frontend without `IOSurfaceLock` makes the kernel "refuse direct
scanout and fall back to the copy path". The source does not say that. With no lease `gpuWriters` stays 0 and the epoch
never changes, so `sweet_surface_present_begin` (`SweetSurface.cpp:324-340`), `sweet_surface_gpu_copy` (:205) and the
direct path (:486, 496) all pass: the kernel proceeds, it just has no protection. Ordering for a swapped surface then
rests only on patch 0013's swap wait. For Stage 3 the missing piece is the other direction: nothing stops NativeMetal
from rendering into a surface while VIG0 is fetching it (the "writers wait while queued/pending/fetched" rule lives in
the writer-lock path that NativeMetal never enters). So the writer lease (fix candidate C) is a prerequisite for Stage 3,
not for the takeover itself. Inference from source, not run.

What upstream reports on its device (docs, NOT reproduced here): gpu-scanout.md:1342-1350, with `fuxipanel120=1
fuxivsyncte=1` the TE heartbeat is ~120/s (period 8266..8280 us), CA paces at ~8.3 ms from `displayMinRefreshInterval`,
and the result is "throughput-limited (wall p50 15..24 ms), not capped at 60". Measured shown rates: 32.8 / 38.9 Hz
(Q222/R223, :1509-1515); "about 84 Hz" for O220 is the panel START rate inferred from a log (:1424). No upstream entry
shows a steady 120 Hz. Stage 2 (one translated flip) was observed on device (X232); Stage 3 was not.

Distance to 120 Hz (8.33 ms/frame) on this device, from experiment 3 numbers:
1. Display cap 60 Hz -> needs takeover + panel120 + TE. Boot args only. Never run on this device.
2. Present blit ~10.7 ms submit->fence per frame -> only Stage 3 removes it. Needs the writer lease in NativeMetal
   (frontend patch + CI + deploy) and is unobserved upstream.
3. Render job ~10.5 ms (of which ~2.8 ms cache clean, ~4.3 ms waiting behind the copy) -> the copy wait goes with
   Stage 3, cache clean with bit 19 or 6, the rest is GPU clock (levels 5..8 untested here).
Steps 2 and 3 both have to land before a frame can fit in 8.33 ms. Step 1 alone leaves the ~21 ms serial chain; on
8.33 ms ticks that rounds to 25 ms = 40 Hz (arithmetic only, not measured).

## 2026-11-13 experiment 4: display takeover + panel 120 Hz + TE vsync (boot args only) - RUN

User approved ("已切換，跑實驗 4") after switching the session permission mode (shell / Edit work again). One bounded
temporary boot. Root image stays 0013 (`b5c10b14...`), nothing written to the phone.

### Loader
- `E:\Codex-Xiaomi13-GPU-20261109\prepare-scanout120-loader.py` -> `native-scanout120-loader\`. Source payload = the
  experiment 3 payload (`788caa2f...`); only the boot args grow by 80 bytes:
  ` fuxigpuscanout=1 fuxiscandouble=1 fuxiscantriple=1 fuxipanel120=1 fuxivsyncte=1` (540 of 607 characters). Image is
  byte-identical to the experiment 3 Image except those 80 bytes (asserted in the script).
- payload sha256 `3ed072d301bad25712d0e4623de1b4248906c6ec9a926f9cba7e83fef1209606`
- boot.img sha256 `396a2ab91b1f4fb8a789dcbebdae3711b0bb66dfdc9669dc278a12c2fc8500d2`
- Pre-boot: `map` OK (`/dev/block/dm-8`), `verify-native-map.py` OK (inode 199819, 39 extents),
  `verify-scanout120-payload.py` OK, unmapped again. Battery 100 %, 36.9 C.
- Boot 12:09:59, monitor 240 s ended 12:14:02 (`monitor_exit=0`, usual `USBError: Entity not found` at the end), Android
  `boot_completed=1` 12:14:45, `mapped=false`, no monitor process.
- Evidence: `native-layout0013-scanout120-boot-1\` (boot.log 13976 lines, sha256 `58488c46...`, thermal.jsonl,
  progress.jsonl) and `native-layout0013-scanout120-monitor-1.log`. All numbers below were grepped by the main agent
  (no separate Gemini evidence report). **The user's visual impression of this boot has not been collected yet.**

### What worked (measured)
- Takeover: boot.log:83 `FUXI SCANOUT: requested, fence then manual transfer, drain_us=20000 te_gate=0`; :1589 `owned,
  autorefresh disabled; draining inherited transfer`; :1653 `double=1 A=0xb8000000 B=0xb8a00000 ... triple=1
  C=0xb9400000`. No `FUXI SCANOUT: stop`, no `drain timeout`, no `te counter stalled`.
- Panel: :1592 `panel120 begin`, :1593 `panel120 switched vsync=0x180085->0x180042 ... period_us=8333 drain_us=10000`.
  First time this kernel drove DSI commands into this panel; it accepted the switch. `te_hz=120.8/120.9` in all six
  `waits per 600 starts` lines.
- Vsync: :76 `vsync TE requested offset_us=7800`; 43 lines `SWEET FB: vsync te-locked=600 fallback=0 ... period_us
  8264..8275` (one window `te-locked=596 fallback=4`, right after the backboardd respawn). :81 `IOMFBDisplayRefresh
  displayMinRefreshInterval=35791394 (120 Hz)`. (:78 still prints `present up to 60 Hz`: that text is `60 / fFlushTicks`.)
- 401 MHz still acked: :139 `level=4 ... requested_khz=401000 ... hfi_ack=1`.
- Zero: `FUXI GPU HANG`, `FUXI GPU: fail`, `HFI error`, `0x90a`, `fence timeout`, `GPU fault`.
- SRC0 readback matches in every window, `mismatch=0`; 4 `double CPU fallback` / `double re-entered` pairs.

### Comparison with experiment 3 (same root image, same 401 MHz, same queue args)
164 `FUXI TOUCH: contact` lines (experiment 3: 99) - more swiping, NOT a controlled comparison. Windows 1-4 are before
the backboardd crash, window 5 contains it, window 6 is after it (backboardd on the CPU path, see below).

| metric | exp 3 (no takeover) | exp 4 windows 1-4 | exp 4 window 5 / 6 |
|---|---|---|---|
| `SWEET FB: stats` 600 frames in ms | 106853 / 37167 / 39267 / 30834 | 82642 / 22633 / 28153 / 25957 | 27898 / 45098 |
| -> hz (includes idle time) | 5.6 / 16.1 / 15.3 / 19.5 | 7.3 / 26.5 / 21.3 / 23.1 | 21.5 / 13.3 |
| copy avg us | 11980 / 12202 / 12239 / 13338 | 1994 / 1874 / 1819 / 1878 | 2778 / 1985 |
| stages submit_fence_us avg (present blit) | 10688 / 10789 / 11108 / 12141 | 1162 / 1192 / 1112 / 1177 | 1152 / 1219 |
| GPU retire avg us | 15909..15997 | 1112..1194 | 1170 / 1226 |
| presents GPU / CPU | 401/195, 405/195, 417/183, 458/142 | 604/2, 660/0, 642/0, 601/0 | 660/2, 728/0 |
| swaps / not shown | 599..600 / 0 | 611/12, 660/60, 642/42, 601/1 | 666/67, 728/128 |
| native swap wait avg us (cumulative n=600..) | 10178 / 10876 / 10457 / 11275 | 10453 / 8957 / 7996 / 10081 / 11525 | (no line after the crash) |
| fence waits per 600 avg us | 9587 / 10979 / 9639 | 9723 / 6733 / 6076 / 16238 / 16072 | - |
| submit_split total / retry_sleep us, attempts | 7190.3 / 4346.1, 5.0 | 2368.2 / 13.7, 2758.9 / 24.8, 2185.4 / 0.4, 2448.4 / 0.0, 2118.6 / 16.5; 1.0 | - |
| refusals busy per 600 | 2410 / 2444 / 2071 / 2442 | 14 / 25 (first two) | - |
| cache clean us/submit (rw + ro) | 2778..3468 | 2280 / 2654 / 2113 / 2363 / 2001 | - |
| `backboardd frames=` best window hz (gpu_ms) | 31.39 (6.9) | 59.13 (5.0) | - |
| START-to-START gaps <=1 / <=2 / <=3 / longer panel periods | n/a | 0/138/187/274, 0/266/151/183, 0/190/89/321, 0/36/70/494 | 0/125/116/359, 0/151/11/438 |

`backboardd frames=` windows in this run (hz): 10.38, 8.05, 31.73, 25.44, 37.20, 59.13, 38.56, 16.14, 9.91, 37.19,
28.55, 27.45, 22.00, 28.45, 35.97, 24.56; gpu_ms 3.8..19.2 (rising to 14..19 ms in the last third); late 1060 by
boot.log:10915. Experiment 3: 31.39, 19.44, 26.29, 8.00, 11.22, 30.08, 24.85, 21.00, ..., gpu_ms 5.3..20.2.

### Reading
- Measured: the ~10.7 ms "present blit" and the 16.0 ms "GPU retire" of every earlier run were mostly waiting for the
  16.67 ms tick. Under kernel ownership the copy completes in ~1.2 ms, and client submits no longer queue behind it
  (retry_sleep 4346 -> 0..25 us, attempts 5.0 -> 1.0, busy refusals ~2400 -> 14..25 per 600). So item 2 of the
  "Distance to 120 Hz" list above was wrong in size: the per-frame copy costs ~1.2 ms, not ~10.7 ms. Stage 3 (no copy)
  is worth ~1.2 ms per frame, no longer the main item.
- Measured: shown rate per 600-frame window rose (16-19.5 -> 21-26.5 Hz, uncontrolled), best `backboardd frames=` window
  31 -> 59 Hz. The CPU presents (142..195 per 600) are gone.
- **Still a 60 Hz cap in this configuration, by construction** (source, `fuxi_gpu_scanout.inc`): `fg_panel120_switch`
  sets the per-START floor to `FG_SCAN_DRAIN_US/2` = 10 ms (:1786, logged as `drain_us=10000`), which is longer than the
  8.33 ms panel period; the comment at :787-788 says exactly this ("a START finishing its transfer just before a TE
  waited out the floor and missed that TE"). The first gaps bucket (<= 1.125 period = 9.37 ms, :2106-2111) is 0 in all
  six windows (0 of 3600 STARTs); the best `backboardd` window sits at 59.13 Hz with gpu_ms 5.0. Ways around it, none
  run here: `fuxigpuncimport` bit 13 (8192, floor = 3/4 period = 6.25 ms, :52, :789-790), `fuxiscandrain=<4000..20000>`
  (:151), or the TE gate `fuxiscante=1` (floor 0, :145-150; falls back to the 10 ms floor if the TE counter stalls,
  :1886-1895).
- Inference: after that floor, the limiter is the client render job: swap wait avg 8..11.5 ms, fence waits 6..16 ms,
  gpu_ms up to 19 ms, cache clean ~2.0..2.7 ms per submit. Most STARTs are 3 or more periods apart.

### New failure: BO slot table full -> backboardd crash -> rest of the boot on the CPU path
- boot.log:11315-11346, at ~192 s: 8 x `FUXI GPU: host import refused stage 3 kr 0xe00002be`; stage 3 = no free BO slot
  (`FuxiAdrenoUserClient.cpp:369-384`, `freeSlot()` returned null), `FG_ABI_MAX_BOS` = 1024 (`fuxi_abi_wire.h:13`).
  :12203 `peak_bos 1024`. NativeMetal reports `no-copy buffer refused ... HostMemory NotRepresentable` (9 x), then
  `command buffer refused: setRenderPipelineState: null handle` (:11339), then
  `CRASH backboardd[56] sig 11 ... pc 0x107e2e8c4 lr 0x107e2e898 far 0x0` (:11355-11357). Not the experiment 1/2
  `tu_init_hw<A7XX>+0xcc` signature (low bits `ecfb4 / ecfa0 / ec650`).
- :11319 `backboardd gate runtime-failure next-respawn=CPU persisted=1`; after respawn :11431 `backboardd gate deny
  reason=strikes strikes=1`. So the last ~48 s of the boot (window 6 and part of window 5) had backboardd rendering on
  the CPU; the kernel present copy still ran on the GPU. The denial is boot-scoped (source,
  `D:\xiaomi13\fuxi_ios\ios-fuxi\tools\fuxi-metal\BackboardGate.hpp:57-82`): the record in `gate.state` is keyed by
  `kern.bootsessionuuid` and a different UUID logs `new-boot reset`, so the next boot should start on the GPU path
  again. Not observed yet; confirm with `gate allow armed` early in the next boot.log.
- `live_bos` grows through every run, it is not new: 341 / 594 / 659 / 654 (0013 queue 220 MHz), 344 / 477 / 535 / 601
  (exp 3, `peak_bos` 724 at 2404 submits), 297 / 447 / 460 / 530 / 684 (this run, `peak_bos` 683 at 2786 submits, 1024
  by 3438 submits). This run simply submitted more in 240 s. `imports` per window stays 98..133 and `mapped_mb` 54..74,
  so the growth is not in imports. Leak vs. legitimately live objects (more apps opened by the touch script / the user)
  is NOT established. Either way it ends every session after ~3400 submits, sooner at a higher frame rate.
- Also new in the CRASH list: `PosterBoard` sig 6 x 4 (first :6736) and `AegirPoster` sig 6 x 11 (first :6887), both
  abort with the same pc `0x1ffc27198` / lr `0x20f1b65f8`, long before the BO refusals. Cause not established. Usual
  ones unchanged (bluetoothd, fairplayd.H2 sig 9 x 21 each, driverkitd x 1, assetsd sig 6 x 10).
- Thermal (24 samples, 10 s apart): max 80.5 C cpu-1-9 at +230 s (after the crash, backboardd on CPU), 73.4 C at +120 s
  and +210 s; GPU sensors max 60.1 C. Earlier runs peaked at 63-68 C. Hotter than any previous run even before the crash.

### Distance to 120 Hz after this run (replaces the list above)
1. Display path: panel and vsync run at 120.8 Hz. Remaining cap = the 10 ms START floor (boot arg, see above).
2. BO slot exhaustion: blocks any run longer than ~3400 backboardd submits. Needs the cause (frontend BO lifetime) or,
   as a stopgap, a larger `FG_ABI_MAX_BOS` (kernel rebuild).
3. Render job: has to drop from ~8-11.5 ms swap wait (gpu_ms up to 19 ms) to well under 8.33 ms. Candidates, none run:
   bit 19 dirty tracking / bit 6 coherent self-test for the ~2-2.7 ms cache clean, `fuxigpulevel` 5..8 (watch the
   temperature: 73-80 C already at level 4).
4. Stage 3 no-copy: ~1.2 ms per frame, needs the writer lease first. Lowest priority now.

## 2026-11-13 BO slot exhaustion: kernel side and log evidence (main agent); frontend survey delegated

User chose "先查 BO 用盡". Read-only, nothing booted or written.

Kernel (`kernel-source-979\xnu\fuxi-adreno\`, read directly):
- Only two selectors take a slot of the per-client table: `alloc` (`FuxiAdrenoUserClient.cpp:306-363`, rounds every
  request up to 16 KiB, one BO each) and `import` (:368-, wires a client VA range). `free` -> `drop` (:285-304) returns
  it at once unless the GPU unmap is busy (`kIOReturnBusy`) or the BO has CPU leases (:470). `clientClose` drops all.
- `liveBos_` is per client, `peakBos` is the maximum over all clients this boot (:44-45, :194-201).
- `FG_ABI_MAX_BOS` 1024 (`fuxi_abi_wire.h:13`) sizes: the per-client table (`IONewZero(Bo,MaxBos)`, :593), the handle
  index (`FuxiHandleIndex<MaxBos>`, 2 x slots, needs a power of two, `FuxiHandleIndex.h`), and the global map list
  `fg_abi_maps[FG_ABI_MAX_BOS]` (`fuxi_gpu_abi_backend.inc:113`), which is walked linearly by `fg_abi_map_note`,
  `fg_abi_map_forget` and `fg_abi_map_at` (:268-286). Separate limits: `FG_ABI_MAX_IMPORTS` 512 mapped imports,
  `FG_ABI_MAX_WIRED_BYTES` 768 MiB. Source comment at :44: peak_bos "decides whether FG_ABI_MAX_BOS must grow".
- So the limit is one constant. Raising it means a kernel rebuild (the harness Mac workflow that produced
  `kernel-surface-artifact`, run 42569395900) and a repacked loader; the kernel travels in the loader payload, so no
  write to the phone's root image. Not done, not approved.

Log evidence (`FUXI GPU ABI: calls n/avg/max` per ~600 submits; alloc / free / import -> net = alloc + import - free):
- exp 4: 344/251/355 -> +448, 21/211/195 -> +5, 88/613/595 -> +70, 151/66/70 -> +155, 404/329/271 -> +346. Sum 1024 =
  `peak_bos` at the refusal. The net of each window equals the `live_bos` growth, so the counter is consistent.
- exp 3: 373/252/308 -> +429, 103/205/196 -> +94, 23/406/348 -> -35, 175/191/239 -> +223. Sum 711 (`peak_bos` 724).
- Reading: not a fixed per-frame leak. ~430-450 BOs are taken during boot (lock screen, SpringBoard, posters), then
  growth comes in bursts and one window even shrank. In exp 4 the last burst (+346, boot.log:9482-11315) coincides with
  `exec MobileSlideShow` / `splashboardd` / `PosterBoard` (an app launch). Whether those BOs would have been freed
  later is not known: the table filled first.
- `FUXI MTL: backboardd frames=` reports `tex_pool_*`, `slab_*`, `buf_pool_*` all 0 in this run: the NativeMetal path
  does not go through those FuxiMetal pools (to be confirmed by the frontend survey).

Frontend survey (which Metal objects map to how many BOs, suballocation, deferred frees, what happens after a refusal,
existing mechanisms) delegated to Gemini 3.8, read-only; task file `gemini-bo-slots-task.md`, response
`gemini-bo-slots-response.json` in the session scratchpad
`%USERPROFILE%\AppData\Local\Temp\claude\7796098606ca2633\546378f2-87be-4887-b16e-ea2ef396da90\scratchpad\`.
Result pending at the time of writing.

## 2026-11-13 BO slot exhaustion: Gemini survey checked against the deployed build; the slots are held by `alloc` BOs

Read-only. Nothing booted, built or written to the phone.

Delegation: Gemini 3.8 (`antigravity/gemini-3.8-flash`, via Magpie, Read/Grep/Glob only). Raw report saved as
`E:\Codex-Xiaomi13-GPU-20261109\perf-0013\gemini-bo-slots-survey.md` (34474 bytes). 116 turns, 20.1 min, input 339886,
output 64990, cache read 10191437 tokens; it reported 115 tool calls against the 80 asked for.

What was checked by the main agent, and how it came out:

1. Frontend citations (deployed backport, `xiaomi13-layout-backport-20261112\native\NativeBridgeMetalFrontend.mm`):
   correct. `gSurfaceImports` is a strong-to-weak map capped at 256 entries (:224-246); plain buffers and textures
   call `nmb_buffer_new` / `nmb_texture_new` (:1456, :1462, :1479); a no-copy buffer and each IOSurface import call
   `nmb_buffer_new_no_copy_releasing` (:235, :1468); a refused no-copy buffer logs through `NBLOG` and returns nil with
   no copy fallback (:1471); `NBLOG` -> `noteProblem` (:93-100); once `fuxiBlocked()` (:34) is true,
   `newRenderPipelineState...` hands back an empty `[NBRenderPipeline new]` (:1560-1569). That matches the boot.log
   order: import refused -> `setRenderPipelineState: null handle` -> sig 11.

2. metal2vulkan allocator: Gemini cited the wrong version. Its figures (`FIRST_BLOCK_BYTES` 4 MiB growing blocks,
   `SPARE_FOR` 10 s, `release_kept_memory`) come from bridge patches 0013/0021/0030 in the newer upstream tree
   `D:\xiaomi13\fuxi_ios\UniXNU` (depth-1 clone at 73b35d2, 2026-11-07). The deployed stack applies bridge patches 0001-0007 only
   (`native\bridge\patches\`, `build-fuxi-nativemetal.py:23-26`) plus `metal2vulkan-adrenometal-0009..0015`, and none
   of those names occurs in the backport directory. The source that was actually built is in the share directory:
   `native-layout0013-share\native\metal2vulkan-source.tar.gz`,
   `metal2vulkan/crates/metal2vulkan-runtime/src/memory.rs`: `BLOCK_BYTES = 64 << 20` fixed (:52); resources share
   pooled blocks, a request above half a block gets a dedicated one (:7-10, :1376-1377); "Imported host memory ... one
   dedicated block per import, never pooled" (:13-15). So on this phone ordinary Metal buffers and textures cost very
   few BOs, and each no-copy buffer / IOSurface import costs exactly one kernel `import` slot. The two mechanisms in
   Gemini's section E that depend on 0021/0030 (`release_kept_memory`, lowering `SPARE_FOR`) do not exist here.

3. Turnip: Gemini again cited the newer file. The deployed `mesa\libvulkan_freedreno.dylib` (sha256 `74670207...`,
   17878576 bytes, dated 2026-11-08) is built from the pinned UniXNU commit `fb6aad6a...`, whose
   `turnip-darwin-kmd.patch` hashes to `f3bc6d47...` (`build-fuxi-turnip.py:17,23`). The local copy hashes to
   `da264be3...` and the pinned commit is not in the local clone. The pinned `tu_knl_darwin.cc` is much shorter:
   `darwin_bo_init_host` is at :418 there (`mesa-fuxi-hostimport.patch` hunk header) against about :877 in the local
   copy. What can be said about the deployed binary: it contains the strings `TU_DARWIN_BO_CACHE` and
   `NativeAdreno BO cache: %llu allocations, %llu reused ...`, so a BO cache exists and is on by default. Whether it
   has the rule "evict what was not reused within a second" is NOT known. The local, newer patch says of that rule
   (:892-895) that without it "the cache held every BO the render server ever freed" (backboardd 3099 MiB), i.e. an
   earlier version behaved that way.
   In the local version the cache keeps "kernel handle, GPU mapping, CPU lease" of a freed BO (:347-353), so a cached
   BO still occupies a kernel slot; host imports never enter the cache; `BoAlloc` failure flushes the cache and retries
   (:1061-1093); `BoImport` failure does not, it returns `VK_ERROR_INVALID_EXTERNAL_HANDLE` at once (:1166-1172).
   No boot-arg or config route sets `TU_DARWIN_BO_CACHE` for backboardd (it is a plain environment lookup; the only
   `setenv` calls for Turnip are in code: `fuxi-metal\Backend.hpp:388-397`, `ClientSetup.hpp:11`).

4. New log evidence, which decides who holds the slots. `submit_split ... imports N` and
   `FUXI GPU: presents+ ... imports N` are a snapshot of the live host imports (`fuxi_gpu_abi_backend.inc:1149-1150`).
   Combining it with the per-window call counts (import frees = imports made - growth of live imports; alloc frees =
   frees - import frees):

   | run / window (boot.log line) | alloc | import | free | live total | live imports | alloc frees | live alloc BOs |
   |---|---|---|---|---|---|---|---|
   | exp 4 / :4678 | 344 | 355 | 251 | 448 | 131 | 27 | 317 |
   | exp 4 / :6327 | 21 | 195 | 211 | 453 | 128 | 13 | 325 |
   | exp 4 / :8024 | 88 | 595 | 613 | 523 | 120 | 10 | 403 |
   | exp 4 / :9482 | 151 | 70 | 66 | 678 | 133 | 9 | 545 |
   | exp 4 / :12200 (spans the crash) | 404 | 271 | 329 | 1024 | - | - | - |
   | exp 3 / :4608 | 373 | 308 | 252 | 429 | 78 | 22 | 351 |
   | exp 3 / :6254 | 103 | 196 | 205 | 523 | 93 | 24 | 430 |
   | exp 3 / :8525 | 23 | 348 | 406 | 488 | 58 | 23 | 430 |
   | exp 3 / :10205 | 175 | 239 | 191 | 711 | 125 | 19 | 586 |

   Host imports churn and stay at 55-133 live. `alloc` BOs are almost never returned to the kernel: exp 4 made 604 in
   the first four windows and freed 59; exp 3 made 674 and freed 88. At exp 4's fourth window 545 of 678 slots (80 %)
   are `alloc` BOs, and the last window added 404 more allocs (the Photos launch) until the table was full. The first
   call to fail was an import only because imports are the frequent call; the import path is also the one without a
   flush-and-retry.

Corrected reading:
- Gemini's emphasis (no-copy buffers and IOSurface imports bypass pooling, so they fill the table) is wrong for this
  build. They do take one slot each, but they are released promptly. The table is filled by ordinary `alloc` BOs.
- Those are not metal2vulkan's pooled blocks (64 MiB each, few). By elimination they are Turnip's own BOs (command
  streams, descriptor pools, pipeline/shader storage and the like) and/or freed BOs parked in Turnip's BO cache.
  Inference, not established: the log cannot tell a live BO from a cached idle one.
- The near-zero free count is what a cache without idle eviction produces. It is also what genuinely live BOs
  produce. Not distinguished.
- Unchanged from before: one refused import latches the gate and backboardd dies; there is no copy fallback.

Directions, none started, none approved:
- A. Raise `FG_ABI_MAX_BOS` (power of two; 4096 suggested so growth can be watched). One constant, kernel rebuild on
  the harness Mac workflow, repacked loader, temporary boot; no write to the phone. Removes the crash at ~190 s and,
  with the same log lines, shows whether live `alloc` BOs level off (genuine need) or keep climbing (cache or leak).
  To check when doing it: per-client table memory (`IONewZero(Bo,MaxBos)`), the linear walks of `fg_abi_maps[]`,
  and that the 768 MiB wired quota is not reached first.
- B. Fix Turnip: take the newer `turnip-darwin-kmd.patch` (idle eviction) and add flush-and-retry to the import path.
  Mesa rebuild on a Mac runner plus a root-image write of the 17.9 MB dylib. This is the root-cause fix if the cache
  is what holds the slots; A tells whether it is.
- C. `TU_DARWIN_BO_CACHE=0` as a diagnostic: needs a `setenv` in the plugin (bundle rebuild and root-image write), and
  the source says it costs a BoAlloc + BoMap (about 1 ms) per allocation, so not a 120 Hz configuration.
- D. Make a refused import non-fatal in the frontend (copy fallback): code change, treats the symptom only.

Scratch (not part of the evidence set): the runtime sources were extracted from the tarball to
`%USERPROFILE%\AppData\Local\Temp\claude\7796098606ca2633\546378f2-87be-4887-b16e-ea2ef396da90\scratchpad\m2v-src\`.
## 2026-11-13 Experiment 5: kernel with 4096 BO slots + START floor bit (one temporary boot, nothing written)

User approved "BO 4096 + floor together": direction A from the section above plus `fuxigpuncimport` bit 8192, one
bounded 240 s boot, no write to the phone. Analysis by the main agent (the survey of the kernel patch was delegated to
Gemini, report at `E:\Codex-Xiaomi13-GPU-20261109\perf-0013\gemini-bo4096-kernel-patch.md`; spot-checked, `sizeof(Bo)`
not recomputed).

### Kernel
- The kernel CI builds from `xnu/patches/*.patch` (`tools/ci/prepare-kernel.py`), the loose files under
  `xnu/fuxi-adreno/` are mirrors. `FG_ABI_MAX_BOS` is defined twice inside `0098-fuxi-native-adreno-abi.patch`
  (`bsd/dev/fuxi_abi_wire.h` and `iokit/Kernel/arm/fuxi_abi_wire.h`); both and the loose header go 1024u -> 4096u.
- Harness repo `scyx2611/fuxi-metal-frontend-check-20261108`, branch `codex/kernel-bo-slots` (from main 7a06fe8),
  commit 6143d1f: `patches/kernel-bo-slots.patch`, `tests/test-bo-slots.py`,
  `.github/workflows/kernel-bo-slots-check.yml`. A workflow that is not on the default branch cannot be dispatched,
  so the workflow also has an `on: push` trigger limited to that branch and those paths. Local worktree:
  `%USERPROFILE%\Documents\Codex\xiaomi13-ios-gpu-20261108\kernel-bo-slots-work`.
- CI run 43381086305 (push, success, about 10 min). Artifact `fuxi-kernel-bo-slots` unpacked to
  `E:\Codex-Xiaomi13-GPU-20261109\kernel-bo4096-artifact\`: `kernel.development.fuxi` sha256
  `44a86def9d66cbc787f87801bb4cfdc3afdabae31760dc0c8e515502d7390651` (17451032 bytes), unstripped
  `65cd2bbd1278bcbf8bf303b85a7ea6413657ac3a8e48176f102fba2c3fd3008f`. `cmdline.txt` identical to the previous
  artifact; template `fuxi.adt` differs only in `/chosen:random-seed`.
- Evidence the constant took effect: every text segment has the old size, `__DATA` vmsize 0x5f4000 -> 0x60c000
  (+96 KiB = `fg_abi_maps[]` 1024 -> 4096 entries x 32 B).

### Loader
- `E:\Codex-Xiaomi13-GPU-20261109\prepare-bo4096-loader.py` and `verify-bo4096-payload.py`. Private ADT and boot args
  come from the experiment 4 payload (sha256 `3ed072d3...`); the only change is `fuxigpuncimport=1536` -> `9728`
  (adds bit 8192: panel120 START floor 10 ms -> 3/4 period = 6.25 ms, framebuffer polls START retries at 1/64 tick;
  `fuxi_gpu_scanout.inc:52`, `:787-790`). Loader body before the payload is byte-identical to experiment 4.
- `E:\Codex-Xiaomi13-GPU-20261109\native-bo4096-loader\`: `payload.bin` sha256
  `8c4eb46a42caf2736a3f7ed7497e084d9e552c2bb8dba2d2376baa715cf216af`, `boot.img` sha256
  `a36ebd28dffcace685c69869d1ae1155f76a7514a0952c89549f359eab4c58f2`.
- Pre-boot: mapping re-acquired and checked with `verify-native-map.py`, payload ADT extents equal to the fresh
  `managed-native-map-current.json`, then `sync; unmap`, `adb reboot bootloader`, `fastboot boot`.

### Run
- Boot 14:58:59, monitor 240 s. Evidence: `E:\Codex-Xiaomi13-GPU-20261109\native-layout0013-bo4096-boot-1\`
  (`boot.log` 12077 lines, sha256 `dfacaf77f9155b8cbf00ef496d09286e6f463f71838f9031b6db832700b0aa83`,
  `thermal.jsonl`, `progress.jsonl`) and `native-layout0013-bo4096-monitor-1.log`. Phone back in Android
  (`boot_completed=1`, image present, `mapped=false`).
- Less interaction than experiment 4: 109 touch-downs / 86 contacts here against 186 / 164 there. Keep that in mind
  for every comparison below.

### What the log shows
- Clean: 0 `FUXI GPU HANG`, 0 `FUXI GPU: fail`, 0 HFI errors, 0 fence timeouts, 0 GPU faults, 0 `host import refused`,
  0 `FUXI SCANOUT: stop`, 0 drain timeouts, 0 `te counter stalled`, 0 gate denials, no panic.
- backboardd did not crash; it was on the GPU path to the end (3599 frames, last window at line 12071). Crashes:
  fairplayd.H2 x21 and bluetoothd x21 (both killed by boot arg), assetsd x2, driverkitd x1. Experiment 4 also had
  PosterBoard x4 and AegirPoster x11 between its lines 6736 and 9897, before its backboardd crash; none here.
  Not explained.
- **The larger BO table was not exercised.** `peak_bos` 789 / 874 / 874 / 962 / 990, `live_bos` 481 / 818 / 843 /
  843 / 881 / 958: the run ended just below the old limit of 1024. The absence of the crash is therefore not evidence
  for the 4096 table; what the run does show is that the 4096 kernel boots and runs with no regression.
- BO accounting over the five windows: alloc 948, import 955, free 926, net 977 (experiment 4: alloc 1008, import
  1486, free 1470, net 1024). Same pattern as before: frees keep up with imports, `alloc` BOs are never returned.
  Allocs per 600-submit window: 696 / 75 / 17 / 145 / 15, so they arrive in bursts (new content), not at a steady
  rate, and were still arriving at the end. Whether the count levels off is still open; a longer or busier run
  is needed, and that one would go past 1024.
- Other limits nowhere near: `mapped_mb` 47-56; the `FUXI GPU: presents+` line reports imports 108-150 (mapped
  54-69 MB) against `FG_ABI_MAX_IMPORTS` 512. That this counter is the live import count is my reading of the line.
- **START floor bit works.** `start_bit=1` in every `ready_to_start_us` line (experiment 4: 0). START-to-START gap
  buckets (<=1 / <=2 / <=3 / more panel periods, per 600 starts): 28/121/145/305, 20/174/168/238, 25/151/132/292,
  40/182/138/240, 49/150/148/253. Experiment 4 had 0 in the first bucket in all six windows. So single-period
  (120 Hz) flips now happen, 3-8% of starts. The `waits per 600 starts` line still prints `drain_us=10000`: it prints
  the configured variable, the 3/4-period floor is applied locally at `:789-790`.
- Floor waits per window 200 / 72 / 104 / 151 / 172 (experiment 4: 234 / 752 / 489 / 19 / 721 / 1247);
  `ready_to_start_us` avg 480-674 us.
- The display is no longer what holds the rate down; content is. backboardd windows: best 52.55 Hz (gpu_ms 6.35),
  typical 19-42 Hz, gpu_ms 6.3-13.7 ms. `native swap wait` avg 10.2-10.9 ms (max 36.6 ms; experiment 4 max 66 ms).
  `SWEET FB` 600-frame windows: 97.0 / 23.2 / 18.9 / 60.2 / 34.3 s, best = 31.8 fps (experiment 4 best 22.6 s =
  26.5 fps; activity differs, do not read this as a gain).
- Submit: 1.6-2.1 ms each, of which 1.5-1.9 ms is cache cleaning of 36-47 MB of rw BOs per submit (top three are
  10976 KB each, perm write). Lookup 3.5-4.2 us per submit, `lookup_ns_per_ib` 196-217: the bigger table costs
  nothing on the submit path, as the patch survey predicted.
- Thermal (24 samples): max 59.9 C (cpu-1-3 at +70 s, cpu-1-1 at +140 s), GPU sensors max 53.7 C. Experiment 4 was
  73.4 C before its crash and 80.5 C after. Less interaction and no CPU-path fallback here; the 1/64-tick START
  polling may also matter. Not separated.

### Distance to 120 Hz after this run (replaces the list above)
1. Display path: panel/vsync 120.9 Hz and, with bit 8192, single-period flips occur. No known cap left on this side.
   Keep `fuxigpuncimport=9728` in the diagnostic loader.
2. BO slots: limit is 4096 in the diagnostic kernel, untested above 1024. `alloc` BOs still only grow; cause
   (Turnip BO cache without idle eviction, or live objects) still open. Directions B/C/D above unchanged.
3. Render job is now the bottleneck: backboardd needs 6.3-13.7 ms GPU time per frame and waits ~10.4 ms per swap,
   against a budget of 8.33 ms. The largest known fixed cost is the ~1.5-1.9 ms cache clean of 36-47 MB per submit
   (candidates as before: bit 19 dirty tracking, bit 6 coherent self-test), then `fuxigpulevel` 5..8 (temperature
   headroom looks better now: 60 C peak).
4. Stage 3 no-copy: unchanged, lowest priority.

State: the 4096 kernel and the 9728 loader exist only on the host; the phone is unchanged (temporary boot). The user's
visual impression of this boot has not been collected yet.

User's visual impression of experiment 5 (asked after the run, 2026-11-13): "有變順" (smoother than before). First
positive visual report on smoothness since the 120 Hz goal was stated; experiment 3 (401 MHz) was "no difference",
experiment 4 was never reported. Experiments 4 and 5 differ in two things at once (floor bit, 4096 kernel) plus the
missing backboardd crash, so the impression is not attributed to one of them; the floor bit is the only one of the
two that changes frame pacing.

## 2026-11-13 After experiment 5: two user-reported defects (log triage only) and the prepared experiment 6 loader

User report after experiment 5, alongside "smoother": (1) Settings > About ("關於本機") does not open; (2) in the app
switcher (swipe up) the app window previews are mosaic. Neither had been reported before; whether they predate
experiment 5 is not known (the user had not said they tried these screens in earlier boots).

### Triage from `native-layout0013-bo4096-boot-1\boot.log` and host files (no device access)
- About: `exec Preferences` appears 6 times (lines 3451, 5118, 6312, 7857, 8238, 11114) and there is no `CRASH`
  line for Preferences. Its own output is not in the log: only processes named in `sweetnull=` are captured
  (backboardd, SpringBoard here). So the log cannot say why the pane does not open. Unverified guess: the pane waits on
  a service this port blocks or lacks (`sweetnoexec=bluetoothd,fairplayd.H2`, both killed 21 times; no baseband).
  Cheap diagnostic: add `Preferences` to `sweetnull` for one boot (args 542 -> 554 of 607 characters).
- Switcher previews: the path is the one from patch 0012. SpringBoard is gate-denied (line "gate deny SpringBoard"),
  so its images reach backboardd as foreign ASTC 4x4 data and are imported as plain row-major. This boot logged 8
  `FUXI LINEAR: foreign snapshot imported` lines: 1125x2436 x3 and 846x1827 x5, no refusal or failure anywhere. Per
  boot: 0012 survey 1125x2436 only; 0013 boots add 846x1827 x1; experiment 4 had 192x192 x4; this boot 846x1827 x5.
- 846x1827 is the blurred home wallpaper variant (`Variant-home__CachedSnapshot-MediumGrayTintedBlur...`, HEAD
  0x34e x 0x723, uncompressed `astc` chunk of 4 + 1550144 bytes), not an app preview. The host copy
  (`texture-cache\`, taken 11-13 00:46) decodes to a single flat colour with astcdec, so it cannot confirm or refute
  the row-major assumption for that size.
- App snapshot files are a different on-disk variant that the 0012 offline check never covered: `LZFS` chunk
  (LZFSE-compressed) instead of `astc`, HEAD words 12/13 = 0/1 instead of 1/0, words 15-18 = 0x55555555, sizes
  1125x2436 and 788x1706 ("downscaled"). The host copies are 1-2 KB (flat colour) and predate real app use.
- Not established: which import (if any) carries the app previews, and whether the mosaic is a layout error in the
  0012 import, a decode problem, or a different path. Needs either the snapshot files as they are now on the phone
  image (read-only extraction, then offline decode) or a boot with the user reproducing it while the import lines
  are watched.

### Experiment 6 loader (built on the host, NOT booted, not approved)
- `prepare-dirty19-loader.py` / `verify-dirty19-payload.py`; output `native-dirty19-loader\`: payload sha256
  `0d37a22c17ca267dcb844a220ebf2e33e100fc0f23e655d80c9c9e05a3948620`, `boot.img` sha256
  `614efa2986eb60d68c5cfd900a1251c1c51ce2ec2f745c4f0cf5fa99ac49e8d1`. Byte-identical to the experiment 5 payload
  except the args field: `fuxigpuncimport=9728` -> `534016` (adds bit 19 = 524288).
- Bit 19 (`fuxi_gpu_abi_backend.inc:134-219`, parsed at :582): per-page dirty tracking of import cleans via the pmap
  modify bit; only CPU-written 16K pages of an import are cleaned at submit. One-time self-test, fail closed
  ("import dirty tracking self-test passed/refused"); no other bit required; inactive if the bit 6 coherence
  self-test passes (bit 6 is off here). Upstream's own configurations (2948672/2948680/2948728) include it.
  Expected gain: the three 10976 KB write imports (33 of the 36-47 MB cleaned per submit) are GPU-written, so most
  of the 1.5-1.9 ms clean per submit should go. Stats line when active: "submit_split dirty state ...".
- Risk: a store the modify bit misses would show stale pixels. Do not run it in the same boot as the mosaic
  diagnosis, or the two cannot be told apart.

## 2026-11-13 Preferences diagnostic boot ("prefdiag"): run 15:17-15:21, analysed afterwards from the log only

Written by a later session from the host files; the session that ran the boot left no notes, so what the user saw
on screen during this boot is NOT recorded anywhere and has not been asked yet.

- Loader `native-prefdiag-loader\` (built by the new generic `prepare-args-variant-loader.py`, checked by
  `verify-args-variant-payload.py`): experiment 5 payload with one token swapped (`swaps.txt`):
  `sweetnull=backboardd,SpringBoard,peakpowermanagerd,fuxi-ca-smoke` -> same + `,Preferences`. Same 4096 kernel
  (`44a86def...`), `fuxigpuncimport=9728`, no bit 19. payload sha256 `e360b581...4b4e74a`, boot.img `88b2d025...1357b`.
- Output `native-layout0013-prefdiag-boot-1\` (boot.log 8314 lines) and `native-layout0013-prefdiag-monitor-1.log`.
  4-minute bounded boot; the monitor's final traceback (`USBError: Entity not found` on `display-after.bin`) is the
  device leaving at the deadline, `monitor_exit=0`. Phone is back in Android (`adb devices`: <序號> at 15:31).

### About pane: Preferences neither crashes nor prints an error
- `exec Preferences` twice (lines 3389 pid 284, 6655 pid 305). No `CRASH Preferences` line in the whole log.
  Crashes in this boot: bluetoothd x21, fairplayd.H2 x21 (the `sweetnoexec` kills), driverkitd x1, assetsd x2.
  backboardd and SpringBoard did not crash.
- Captured stderr of pid 284: ManagedConfiguration lines while the root list loads (3606-3662), then directly after
  the tap at 895,412 (line 3974) a burst of ExternalAccessory/IAP init lines (3996-4043: `IAPAppRegisterClient ...
  registerWasSuccessful 0`, `Returning connectedAccessories count 0`), then nothing until `XPC connection
  interrupted` (5596, 21 s later by its own timestamps). pid 305 shows the same ExternalAccessory burst (7201-7242)
  directly after the tap at 173,597 (7168), then `_applicationDidEnterBackground` 2 s later (7396).
- Reading (inference, not proven): the ExternalAccessory burst appears exactly once per process and right after a
  tap in the General list, so it most likely marks the About controller starting to load. After it the process
  stays alive and prints nothing. So "does not open" is not a crash; it is a pane that starts loading and never
  completes or never gets pushed. stderr does not say what it waits on (os_log is not captured by `sweetnull`).
- Still only a guess: a synchronous request to a daemon that is missing or kill-looped here (bluetoothd is the
  obvious candidate since About shows the Bluetooth address; baseband/CoreTelephony is another). The single
  `XPC connection interrupted` fits a kill-looped peer but is not tied to About by anything in the log.
- What would actually decide it: a user-space backtrace of the Preferences main thread while the pane is stuck
  (the kernel already prints `bt` for crashes; an on-demand dump for a named process would need a kernel change),
  or one boot with `bluetoothd` taken out of `sweetnoexec` (boot-args only; why it was put there must be checked
  first). Neither is prepared. This is a functional defect, unrelated to the 120 Hz goal.

### Switcher previews: no foreign-snapshot import happens at switcher time
- 7 `FUXI LINEAR: foreign snapshot imported` lines: 1125x2436 x3 (3248, 3251, 3437: around the first Preferences
  launch) and 846x1827 x4 (7899, 7957, 8011, 8018: around the Safari launch). No refusal or failure.
- Bottom-edge swipes that stop mid-screen (the switcher gesture, e.g. 642,2369 -> 912,1848) are at lines 4820,
  5229, 5455, 5626, 6072, 6811, 7301, 7470, 8004. Only the last one has an import next to it, and that one is the
  846x1827 wallpaper-blur size already identified above. The other eight have no `FUXI LINEAR` line within hundreds
  of lines.
- Reading: the app cards in the switcher are not (re)imported through the 0012 foreign-ASTC path when the switcher
  opens. Either they were imported earlier and cached (only three full-size imports exist, all before the first
  switcher gesture, with Preferences the only user app launched by then), or they come by another route (live
  layer/IOSurface contents of the suspended app, or SpringBoard's own non-gated rendering). The log cannot tell
  which. Whether the user saw mosaic in THIS boot is unknown (not asked).
- This weakens "0012 row-major layout is wrong for app snapshots" as the explanation but does not exclude it for
  the three early 1125x2436 imports. Next evidence is unchanged: the snapshot files as they are now in the phone
  image (read-only extraction, offline decode of the `LZFS` variant), or a screenshot/photo from the user.

### State after this section
- Nothing written to the phone by this boot. Experiment 6 loader (`native-dirty19-loader\`, bit 19) is still not
  booted and not approved. Open questions for the user: what About and the switcher looked like in the prefdiag
  boot; whether to spend the next boot on experiment 6 (smoothness) or on the About/mosaic diagnosis.

## 2026-11-13 Experiment 6: bit 19 dirty tracking - RUN, self-test REFUSED, so the experiment tested nothing

User chose "跑實驗 6". One bounded temporary boot, nothing written to the phone.

- Pre-boot: `map` (`/dev/block/dm-8`), `verify-native-map.py` OK (inode 199819, 39 extents),
  `verify-dirty19-payload.py` OK (only diff to experiment 5: `fuxigpuncimport=9728` -> `534016`), `sync; unmap`,
  `mapped=false`. boot.img `614efa29...9e8d1` checked before `fastboot boot`.
- Boot 15:37:06, monitor 240 s. Evidence: `E:\Codex-Xiaomi13-GPU-20261109\native-layout0013-dirty19-boot-1\`
  (`boot.log` 7039 lines, sha256 `faca10b93bc01cd8854d5bfe5157637a749f82b15dbb48dca85f8cabcc2c17c3`) and
  `native-layout0013-dirty19-monitor-1.log`. Phone back in Android 15:41:53 (`boot_completed=1`, `mapped=false`).

### Result
- Line 1536: `FUXI GPU ABI: import dirty tracking self-test refused, full cleans stay (1)`. It ran on the first
  import, right after backboardd created its 1125x2436 BGRA IOSurface (id 2, 11239424 bytes, line 1532).
- Code 1 (`fuxi_gpu_abi_backend.inc:155-163`): `pmap_clear_modify(ppn)` was called and `pmap_is_modified(ppn)` was
  still true. The modify bit could not be cleared at all; the test never got as far as the store.
- Consequently both stats lines read `submit_split dirty state 3 (...) tracked_imports 0 ... pages_checked 0 ...
  full_kb 27980 / 30396`, and the per-submit cache cost is unchanged: `cache 1058.5` / `1146.9` us (prefdiag boot:
  1065.9 / 1524.4). Fail-closed worked as designed: full cleans stayed, so no stale-pixel risk was taken either.
- Likely cause (from memory of the XNU arm pmap, NOT checked against the 979 source, which is not on this host):
  `arm_force_fast_fault` refuses pages that have a wired or kernel-pmap mapping, and `phys_attribute_clear` then
  leaves the bit set. The driver itself maps every import into the kernel (`pmap_find_phys(kernel_pmap, source...)`
  at `:178` is how it gets the page numbers), so every import page would hit that rule. If that is right, bit 19
  cannot work in this design on any boot, and upstream listing it in its configurations does not show it is active
  there. To confirm: read `arm_force_fast_fault_with_flush_range` / `phys_attribute_clear_internal` in the xnu
  version the CI builds.
- Run health: 0 `FUXI GPU HANG`, 0 `FUXI GPU: fail`, 0 `host import refused`, 0 `FUXI SCANOUT: stop`, 0 `te counter
  stalled`, no panic. backboardd and SpringBoard did not crash. Crashes: fairplayd.H2 x21, bluetoothd x21,
  driverkitd x1, assetsd x2, and AegirPoster x18 (lines 3014-4188, sig 6 x17 + sig 9 x1). AegirPoster also crashed
  in experiment 4 (x11) but not in experiment 5 or prefdiag; still unexplained.
- Little interaction: 37 touch-downs (prefdiag about 90). backboardd windows 15.7-24.6 Hz, gpu_ms 4.7-11.7; swap
  wait avg 8.4-9.8 ms. Not comparable with experiment 5 and not a measurement of anything new. `peak_bos` 673/749.
  Thermal max 62.7 C. `FUXI LINEAR` imports: 1125x2436 x8.
- User's visual impression: not collected yet (expected: same as experiment 5, since the configuration that took
  effect is the same).

### Cause of the refusal: CONFIRMED against xnu-8796.141.3 (replaces the "likely cause" bullet above)
Source read: `osfmk/arm/pmap/pmap.c` of tag `xnu-8796.141.3` (the tag the repo docs link; the CI fetches xnu-8796
via darwin-xnu-build `MACOS_VERSION=13.5`; exact CI tag not re-checked). No file in `xnu/patches/` touches this
code (`grep` for `arm_force_fast_fault`, `FF_WIRED`, `pte_is_wired` in the patches: nothing).
- `pmap_clear_modify(pn)` = `phys_attribute_clear(pn, PP_ATTR_MODIFIED, 0, NULL)` (pmap.c:7245-7249), i.e.
  options 0.
- `phys_attribute_clear_with_flush_range` clears the attribute only if `arm_force_fast_fault_with_flush_range`
  returns TRUE (pmap.c:7010-7012).
- That function sets `result = FALSE` for every mapping whose PTE has the wired bit unless the caller passed
  `PMAP_OPTIONS_FF_WIRED` (pmap.c:7751-7757). My earlier recollection was half wrong: a kernel-pmap mapping is
  NOT refused as such (it is write-protected with AP_RONA at :7769-7774); only the wired bit refuses.
- The driver wires the kernel alias of every import: `createMappingInTask(kernel_task, ...)` then
  `mapping->wireRange(...)` (`FuxiAdrenoUserClient.cpp:417`, `:426`, comment at :422-423 says why: pmap lookup and
  watchdog cache maintenance must not depend on a pageable mapping). `wireRange` -> `vm_map_wire` sets the PTE wired
  bit of that alias.
- So for every import page the modify bit can never be cleared with `pmap_clear_modify`, and the self-test returns 1
  on the first import of every boot. Bit 19 as shipped in this tree cannot become active on this driver, whatever
  the boot args. (Whether the user-side mapping in backboardd is also PTE-wired is not known and does not matter
  for this conclusion.)

Possible fixes, none implemented, both need a kernel rebuild (CI, ~10 min) and a boot:
1. Replace the four `pmap_clear_modify(ppn)` calls with `pmap_clear_refmod_options(ppn, VM_MEM_MODIFIED,
   PMAP_OPTIONS_FF_WIRED, NULL)`. Then the wired kernel alias is write-protected too (`pte_set_was_writeable`) and
   `arm_clear_fast_fault` restores AP_RWNA and sets the bit on the next store (pmap.c:8046-8059). Open risks to
   check in source BEFORE building: (a) the driver's own `dc ivac` at retire runs on that alias; on ARMv8 `DC IVAC`
   needs write permission, so every write-protected page would take a kernel permission fault at retire and be
   marked modified again - that would cost a fault per 16K page and cancel the gain for exactly the GPU-written
   imports (stats: `ivac_us` 950-1047 per submit, so retire IVAC is as large as the submit clean); (b) whether any
   clean/IVAC runs with preemption disabled or under a spinlock, where a fault on the alias is fatal.
2. Leave the kernel alias unwired while bit 19 is on. Contradicts the reason given at :422-423; needs the same
   check of who touches the alias in which context.
Either way bit 19 only removes the submit-side CIVAC; the retire-side IVAC (`ivac_us`, ~1 ms per submit, not in
the earlier 1.5-1.9 ms figure) stays. The cheaper question to answer first is still bit 6 (coherent self-test):
if it passes, both the CIVAC and the IVAC go away with boot args only.

### Distance to 120 Hz after experiment 6 (superseded by experiment 7 below)
Unchanged from the experiment 5 list. Item 3 loses its first candidate: bit 19 is refused. Remaining ways to
remove the ~1.1-1.9 ms clean per submit: (a) bit 6 coherent self-test (if import-class mappings turn out coherent,
no cleaning at all); (b) find out why GPU-written imports need a CPU-side clean at every submit and skip those by
ownership instead of by modify bit (design work, kernel change); (c) make bit 19 workable by tracking through the
user mapping only, which needs the kernel mapping dropped or the fast-fault rule worked around (kernel change, and
only if the cause above is confirmed). After that: `fuxigpulevel` 5..8 and the per-frame GPU time itself.

## 2026-11-13 Experiment 7: bit 6 coherent-import self-test - RUN, wb-ish variant PASSED, import cache work gone

User chose "試 bit 6". One bounded temporary boot, nothing written to the phone.

- Loader `native-coh6-loader\` from `prepare-args-variant-loader.py`, one token swapped against experiment 5:
  `fuxigpuncimport=9728` -> `9792` (adds bit 6 = 64, `fg_abi_cohtest`, `fuxi_gpu_abi_backend.inc:582`, test at
  `:1173-1251`). Same 4096 kernel. payload sha256 `1cde88524c75ac0d1d114608f58c35cab5a0dcd50f91ed9e7f75dcbfde6d079b`,
  boot.img `863c4f22849d118bdee59b259da703b94846aad0a45eb0134054358c0879c4e2`.
- Pre-boot: `map`, `verify-native-map.py` OK (inode 199819, 39 extents), `verify-args-variant-payload.py
  native-coh6-loader` OK, `sync; unmap`, `mapped=false`.
- Boot 15:47:11, monitor 240 s. Evidence `native-layout0013-coh6-boot-1\` (`boot.log` 8526 lines, sha256
  `c42dca6fe1d806bd6f4e428fc5a5dfb849f65f530aa9146dca3aba801e74e590`), `native-layout0013-coh6-monitor-1.log`.
  Phone back in Android 15:51:56 (`boot_completed=1`, `mapped=false`).

### Result
- Line 1536: `coherent-import self-test stale cpu_to_gpu 0/8 gpu_to_cpu 0/8 -> trying write-back inner-shareable`
  (variant 1, today's Normal-NC outer-shareable import attributes: not coherent, as the source comment expects).
- Line 1537: `coherent-import(wb-ish) pass cpu_to_gpu 8/8 gpu_to_cpu 8/8 -> skip import clean/invalidate, imports
  mapped write-back inner-shareable`. From here every import is mapped on the GPU side through MAIR index 1 with
  inner-shareable leaves and gets no CIVAC at submit and no IVAC at retire. First time this path ran on the device
  (source comment: "Not yet observed on device").
- Submit cost: `cache` 2.2-2.7 us per submit in all five windows (experiment 5: 1500-1900, experiment 6: 1058-1147).
  Whole submit is now ~45-50 us (idle 22-24, tlb 9-12, ring 7-14, bell 2).
- backboardd windows (>=100 frames): 18.1 / 25.0 / 29.0 / 43.8 / 44.8 / 26.3 / 75.4 / 32.7 / 32.3 / 35.1 / 36.1 /
  27.8 Hz; gpu_ms 4.4-14.4. Best 75.35 Hz (378 frames, gpu_ms 6.9) against 52.55 Hz in experiment 5.
- `SWEET FB` 600-frame windows after the first: 15.6 / 18.0 / 17.8 s = 38.4 / 33.4 / 33.7 fps (experiment 5 best
  31.8 fps). `native swap wait` avg 5.9 -> 9.1 ms cumulative (experiment 5: 10.2-10.9), max 47.5 ms.
- Interaction: 106 touch-downs (experiment 5: 109), so this run is roughly comparable with experiment 5; still not
  a controlled A/B.
- Health: 0 `FUXI GPU HANG`, 0 `FUXI GPU: fail`, 0 `host import refused`, 0 `FUXI SCANOUT: stop`, 0 `te counter
  stalled`, 0 fence timeouts, no panic, no "poison". backboardd and SpringBoard did not crash; no AegirPoster crash.
  Crashes: fairplayd.H2 x18, bluetoothd x18, driverkitd x1, assetsd x2. One window had 54 `full` submit refusals
  (line 5551, queue full; refused_us 16.5), not seen before; not analysed.
- **BO table above 1024 for the first time**: `peak_bos` 593 / 812 / 816 / 1062. backboardd kept running past the
  old limit, which is the first real evidence for the 4096 kernel. `alloc` BO growth still not explained.
- Thermal max 63.5 C (cpu-1-3, +170 s).
- NOT verified: visual correctness. A pass on one 16K probe page does not prove every import is coherent; stale
  or torn pixels would be the symptom. User's impression of this boot not collected yet.

### Distance to 120 Hz after this run (replaces the lists above)
1. Display path: no known cap (unchanged).
2. Submit-side cache maintenance: gone with bit 6 (boot args only). Keep `fuxigpuncimport=9792` as the new
   baseline if the user saw no artefacts. Bit 19 is moot while bit 6 passes (`:168` disables it then anyway).
3. Now the limit: per-frame GPU time 4.4-14.4 ms against 8.33 ms, and swap wait ~9 ms. Candidates: `fuxigpulevel`
   5..8 (GPU clock; thermal 63.5 C leaves room), then where the frame time goes (IB count per frame is 13-24).
   Bit 29 (cached-coherent BOs, needs bit 6 and the wb-ish pass, both now true) would also give Turnip's own BOs
   write-back CPU mappings; untested, separate experiment.
4. BO growth: table is large enough for a 4-minute run (1062 of 4096); cause still open.
5. Nothing is persistent: the 4096 kernel and these boot args exist only in host loaders.

### User's visual verdict on experiment 7 (2026-11-13) and follow-up analysis
- User, verbatim meaning: app-switcher preview mosaic NOT fixed; smoothness still below target, needs more; no
  other anomaly noticed. So bit 6 showed no stale/torn pixels by eye: `fuxigpuncimport=9792` is the new baseline.
- Mosaic is independent of cache coherency (present before and after bit 6).
- SURF creation census of this boot (`native-layout0013-coh6-boot-1/boot.log`): during the swipe-up gesture
  (lines ~4316-4353) SpringBoard creates IOSurfaces `787x1705 BGRA bpe 4 bpr 3328 size 5685248` x4 (e.g. line
  4343), `788x1706 RGhA bpe 8 bpr 6304` x2, `281x609 fmt 0x77333072` x3, `1065x282 fmt 0x62336138 planes 2` x3,
  several `LA08`; full-screen ones are `1125x2436 BGRA bpr 4608`. bpr is width aligned to 64 px in both cases.
  Almost no `FUXI LINEAR` imports while the switcher is up. Hypothesis (NOT verified): the cards are those
  down-scaled surfaces and the mosaic is a layout mismatch (tiled vs linear) between writer and sampler.
- **GPU is not saturated**: across all backboardd windows of this boot `gpu_busy_est_pct` peaks at 52.0 (the
  75.35 Hz window) and is 20-47 in the 26-45 Hz windows. Meanwhile `fence waits ... poll_us=100 avg_us=9998
  max_us=27909 polls_avg=69`, `fence pending per 600 waits`: 388 of 600 above 8 ms, `irq state 0 ... wakes 0`,
  and `native swap wait` avg 9.1 ms. Reading (NOT verified in code yet): the frame loop is serial (fence wait
  ~10 ms, then swap wait ~9 ms), so the limit is waiting/scheduling, not GPU work. This corrects item 3 of the
  list above: raising `fuxigpulevel` alone should help only in proportion to gpu_ms's share of the frame.
- Clock table (`0098` patch, `fuxi_gpu_clock.h`): level 1..9 = 220/295/348/401/475/550/615/680/719 MHz,
  `G_GPU_LEVEL_MAX` 8 (680). Boot log line 139 confirms level 4 = 401000 kHz, bw_index 2, hfi_ack 1; the vote is
  "not measured".
- Two read-only investigations delegated to Gemini 3.8 via Magpie (task files in the session scratchpad):
  A = mosaic cause in the FuxiMetal frontend IOSurface texture path; B = per-frame timeline, what the fence wait
  and swap wait consist of, and ranked options towards 120 Hz. Results to be appended below when verified.

### Correction of the "GPU is not saturated" reading above, and experiment 8 prepared (2026-11-13)
- `native swap wait` is NOT a second wait after the fence wait. `NativeBridgeMetalFrontend.mm:1051-1058`
  (`fuxiWaitStoredSurface`, patch 0013): for a command buffer that stored into an IOSurface it calls
  `nmb_command_buffer_wait(_handle,0)`, i.e. it blocks the committing thread until the GPU finished that frame.
  The kernel `fence waits` statistic (`FuxiAdrenoUserClient.cpp:254-284`) is the same wait seen from below. So
  swap wait avg 9.1 ms ~= fence wait avg 10.0 ms ~= one frame of GPU work, counted once, not twice.
- `hz` in `FUXI MTL: backboardd frames=` is window_frames / window time and includes idle time, so 26-45 Hz
  windows are partly "nothing to draw". The best window (75.35 Hz, period 13.3 ms, gpu_ms 6.9) is the closest to
  continuous animation: ~7-9 ms blocked on the GPU + ~4-6 ms CPU. `gpu_busy_est_pct` 52 just restates that the
  loop is serial (GPU idle while the CPU encodes and vice versa); it does not mean GPU speed is irrelevant.
- Therefore GPU time per frame IS the largest serial term during animation, and my "about 10 %" estimate for a
  higher `fuxigpulevel` above was wrong. If GPU time scaled with clock, 401 -> 680 MHz would take ~9 ms to ~5.5
  ms and the best-case period from ~13.3 to ~10 ms (~100 Hz). Scaling is not guaranteed (experiment 1, 220 -> 401,
  was not a clean A/B; memory-bound work scales less). At level 8 the DDR row-2 vote rises automatically to DT
  index 7 = 12449218 kB/s (`g_gpu_ddr_row2`, 0098 patch), no `fuxigpuddr` needed.
- 120 Hz still needs either GPU+CPU <= 8.33 ms per frame or a pipelined loop (CPU starts frame N+1 while the GPU
  renders N, flip gated on the fence in the kernel instead of the 0013 synchronous wait). The second is a
  frontend+kernel change, not a boot arg.
- Polling granularity is not the problem: `poll_us=100`, `polls_avg=69` over a 10 ms wait; bit 26 (IRQ sleep,
  `spin()` comment at `FuxiAdrenoUserClient.cpp:251`) would save at most ~0.1 ms per wait. Not pursued.
- **Experiment 8 loader built on the host only (NOT booted)**: `E:\Codex-Xiaomi13-GPU-20261109\native-coh6-lvl8-loader`
  via `prepare-args-variant-loader.py`, swaps `fuxigpuncimport=9728=>9792` and `fuxigpulevel=4=>8` (680 MHz).
  payload sha256 `3351063ca6064864a6570642f61afa8294a6b4213873a89957277e7deae45cf7`, boot.img
  `be570ab57f7357a60c5d751a1d1691e052a7d1e15c4dcb1058ab5bb091b82b97`, same 4096 kernel (`44a86def...`).
  Watch: `FUXI GPU CLOCK: level=8 ... requested_khz=680000 ... hfi_ack=1`, thermal (63.5 C max at 401 MHz),
  gpu_ms / swap wait / best-window hz. Needs the user's go-ahead to boot.
- Delegation: user asked to prefer Gemini 4 Hydrogen, medium (`antigravity/gemini-4-hydrogen --effort medium`);
  the two 3.8-flash runs were stopped after ~50 min without a report; task A (mosaic) restarted on Hydrogen,
  task B (frame time) done by the main agent as recorded here.

## 2026-11-13 experiment 8: fuxigpulevel=8 (680 MHz) on the 9792 baseline - RUN (user approved in chat)
- Loader `native-coh6-lvl8-loader` (payload `3351063c...45cf7`, boot.img `be570ab5...2b97`). Pre-boot: map ->
  `verify-native-map.py` OK (inode 199819, 39 extents) -> `verify-args-variant-payload.py` OK -> unmap,
  `mapped=false`. Battery 100 %, 32.0 C. `fastboot boot`, one monitor, 240 s. Evidence:
  `native-layout0013-lvl8-boot-1\` (boot.log 11059 lines, sha256 `c2086ebc...58a1af`),
  `native-layout0013-lvl8-monitor-1.log` (`monitor_exit=0`, usual USBError at device expiry). Afterwards Android
  `sys.boot_completed=1`, `mapped=false`. Nothing written to the phone.
- Clock accepted (line 139): `level=8 gpu_index=8 requested_khz=680000 gx_arc=0xc00408 bw_index=2
  ddr_kBps=12449218 hfi_ack=1` (still "not measured"). bit 6 wb-ish self-test passed again 8/8 (line 1550).
- backboardd windows (active ones): gpu_ms 4.8-6.5 mostly (7.5-9.8 in three, 13.4-13.6 in two tiny windows);
  hz 67.6 / 56.9 / 52.0 / 58.6 / 61.5 / 48.4 / 35.5 / 58.6 / 48.8 / 25.6 / 22.6 / 23.9 / 23.6. Experiment 7:
  gpu_ms 4.4-14.4, hz 18-45 with one 75.35 window. Total frames 4981 vs 3146 in the same 240 s.
- `late` 121 / 4981 = 2.4 % (experiment 7: 334 / 3146 = 10.6 %).
- `native swap wait` avg 6.0-6.8 ms, max 28.4 ms (experiment 7: 9.1 avg, 47.5 max). Kernel `fence waits` avg
  5.6-8.3 ms (10.0).
- `SWEET FB` 600-frame windows: 15.1 / 12.3 / 15.3 / 20.6 s among idle ones; best 12.3 s = 48.6 fps (experiment 7
  best 38.4 fps).
- Health: 0 HANG / fail / import refused / scanout stop / panic. `peak_bos` up to 1223. 134 touch contacts (more
  interaction than experiment 7's 106; again not a controlled A/B).
- Thermal max 73.8 C (cpu-1-9, +160 s) against 63.5 C at 401 MHz: +10 C in a 4-minute run. Not sustainable as
  a fixed level without the governor (`fuxigpulevel` bit 0x10, cap + idle floor) - untested with level 8.
- Reading: GPU time per frame fell by roughly a third and waits with it, but sustained windows sit at 52-62 Hz,
  i.e. a ~16-17 ms period with only ~6 ms of it blocked on the GPU. Several windows are suspiciously close to
  60 Hz (58.6, 61.5, 58.6, 56.9). Hypothesis (NOT verified): the producer side (backboardd's render cadence /
  CADisplayLink / the display's advertised refresh rate inside iOS) still runs at 60 Hz even though panel and
  vsync TE are 120 Hz; 67.6 and experiment 7's 75.35 show it is not a hard cap. Next thing to establish: what
  refresh rate iOS userland believes the display has, and what drives backboardd's frame start.
- User's visual verdict for this boot: not collected yet.
- Delegation note: user asked for the mosaic task on Haiku 5.5 (high, context <= 100k). Not possible here: the
  built-in Agent tool resolves "haiku" to `antigravity/gemini-3.8-flash` against api.anthropic.com (404), and the
  `claude` CLI has no Anthropic login ("Not logged in"); Magpie lists no Haiku. Hydrogen run was stopped on the
  user's redirect. Mosaic task currently NOT running; waiting for the user's choice.

### App-switcher mosaic: likely cause found (Haiku 5.5 report, key points verified by the main agent)
- Delegated read-only to `claude-haiku-5-5` (effort high, context cap 100k; 103 turns, 6.7 min). Report saved in
  the session scratchpad as `result-mosaic-haiku.md`. Verified by me:
  - Running kernel source `kernel-source-979/xnu/fuxi-scanout/SweetSurface.cpp:779-785,812-815` (= patch `0108`):
    `sweetsurfrow` defaults to 1 and any non-zero value means "round EVERY 4-byte single-plane row up to 256".
    787 px -> 3148 -> 64-aligned 3200 -> 256-aligned **3328**, exactly the `bpr 3328` in the boot logs.
  - The older tree `xiaomi13-ios-gpu-20261108/ios-native-lf/xnu/fuxi-scanout/SweetSurface.cpp:75-76,840-860`
    already has the fix: `sweetsurfrow` default **2** "since 3 Nov" = round only rows that are not 64-byte
    aligned, with the comment "Rounding every row (=1) striped the app switcher's cards: whatever writes the
    decoded JPEG snapshot keeps the row it asked for, so the import read it at the wrong pitch (... =0 fixed the
    cards and blacked App Library out again)". It also logs `SWEET SURF: row A -> B`; no such line exists in our
    boot logs, consistent with the running kernel being the mode-1 version.
  - Frontend takes the pitch from `IOSurfaceGetBytesPerRow` and imports linear (`NativeBridgeMetalFrontend.mm`
    ~1485-1501 per the report; not re-read by me).
- So the bo4096 diagnostic kernel (built from `kernel-source-979` patches) lacks the 3 Nov mode-2 change: a
  regression of a previously fixed bug. Consistent with the user first reporting the mosaic at experiment 5, the
  first boot of that kernel (circumstantial: no record that the switcher was checked before).
- Corrections from the report: the four 787x1705 creations are in separate gestures (lines 4343, 4694, 4735,
  4965), and "almost no FUXI LINEAR while the switcher is up" was not evidence - that log is capped at 8 lines
  (`FuxiTextureLayout.inc:243`).
- Not proven yet: nothing was booted for this. Open points: "blocks" vs the "stripes" a pitch error usually
  gives; 788x1706 RGhA (bpr 6304) is outside the 256 rule.
- Options: (a) boot-arg only `sweetsurfrow=0` - expected to fix the cards and black out App Library (known
  trade-off), proves the cause in one boot; (b) port mode 2 from ios-native-lf into patch `0108`, rebuild the
  kernel in CI (needs a push), which is the real fix. Neither done; waiting for the user.

## 2026-11-13 sweetsurfrow mode 2 ported into the kernel-bo-slots harness (kernel rebuild)

User chose "直接移植重建" after the mosaic analysis above.

- Harness branch `codex/kernel-bo-slots`, commit `bd72669` (pushed `6143d1f..bd72669`, user approved this push).
- New `patches/kernel-surfrow-mode2.patch` (applied after `kernel-surface-stride.patch` and `kernel-bo-slots.patch`
  on `zgl97/ios-fuxi@979e2f0`): six 1:1 line replacements in both `xnu/fuxi-scanout/SweetSurface.cpp` and the
  `+` lines of `xnu/patches/0108-fuxi-gpu-scanout.patch` (the kernel is built from the latter).
  `sweet_surface_gpu_rows()` now returns an int mode, default 2 (values above 2 clamp to 2);
  the 256-byte rounding of bpe==4 single-plane rows is skipped in mode 2 when the row is already 64-byte aligned.
  `sweetsurfrow=1` restores the old behaviour, `=0` disables rounding. No `SWEET SURF: row A -> B` log line was
  ported (the ios-native-lf original has one); check the create log's `bpr` instead.
- New `tests/test-surfrow-mode2.py`; workflow applies the patch and runs the test.
- Expected on the phone: 787x1705 BGRA `bpr` 3200 instead of 3328; 281x609 `bpr` 1152 instead of 1280;
  1125-wide surfaces (4500 -> 4544 after 64-align) also stay at 4544 instead of 4608 -- this is the App Library
  risk the original comment mentions for mode 0, but mode 2 in ios-native-lf was the shipped default there.
- CI run `43398126206` (patches applied OK; build result recorded below).
- Host-side tooling for kernel-only loader variants: `E:\Codex-Xiaomi13-GPU-20261109\prepare-kernel-variant-loader.py`
  `<out> <source loader> <kernel artifact>` and `verify-kernel-variant-payload.py <out>` (same ADT and args as the
  source loader, only the kernel differs; loader body before the payload must be byte-identical).
- CI run `43398126206`: success at `bd72669`. Artifact in `E:\Codex-Xiaomi13-GPU-20261109\kernel-surfrow2-artifact\`:
  kernel `239bf0d99992b8ba392fddbcf43fdf8de02da58d5f776421fd2fb1e6a4f877b4` (matches its `SHA256SUMS`),
  unstripped `d4e9b28d64132822a9780b22a46dd5f8adda6f6ae0d8b9f4e7cac2f2b7cab5fd`, `cmdline.txt` identical to the bo4096
  artifact. The artifact's generic `fuxi.adt` differs from the bo4096 one in 63 bytes from offset 661
  (random-looking field; not used -- the loader carries the private ADT from the source payload).
- Loader `native-surfrow2-lvl8-loader` (host only, NOT booted yet): source `native-coh6-lvl8-loader` (experiment 8,
  same ADT and args incl. `fuxigpuncimport=9792 fuxigpulevel=8`), only the kernel replaced.
  payload `452765cd5f325093a4331a8fbcdd16842a116a6b7e1b8d4c65a9c08f6fec3d16`,
  boot.img `5d40ebd22513d0c938a52fbabf0b6b3b9e5a71ee62aa5ec3f2a57bac57125cf6`.
  Before boot: map -> `verify-native-map.py` -> `verify-kernel-variant-payload.py native-surfrow2-lvl8-loader`;
  monitor `--kernel` must point at the surfrow2 unstripped kernel.
  Checks: app-switcher cards, App Library not black, create log `bpr` 3200 for 787x1705 and 4544 for 1125-wide.

## 2026-11-13 experiment 9: surfrow mode 2 kernel on the experiment-8 loader - RUN (user approved in chat)
- Loader `native-surfrow2-lvl8-loader` (boot.img `5d40ebd2...5cf6`, kernel `239bf0d9...77b4`). Pre-boot map ->
  `verify-native-map.py` OK (inode 199819, 39 extents) -> `verify-kernel-variant-payload.py` OK -> unmap. Battery
  100 %, 33.5 C. Evidence `native-surfrow2-lvl8-boot-1\` (boot.log 13083 lines, sha256
  `8cac879a6b8b7fe04b9fd444c47a93453e70744576c1f36daac6d7d6b6d691d3`), `native-surfrow2-lvl8-monitor-1.log`
  (`monitor_exit=0`). Afterwards Android `sys.boot_completed=1`, `mapped=false`. Nothing written to the phone.
- **User's visual verdict: app-switcher mosaic FIXED, no anomaly seen, smoothness improved but apparently still
  short of the 120 Hz target.** Mode 2 is the new kernel baseline.
- Create log: 787x1705 `bpr 3200` (9x, was 3328), 281x609 `bpr 1152` (was 1280). 1125x2436 single-plane surfaces
  still report 4608 (48x; so their writers ask for a non-64-aligned or already-256 row); 4544 appears only for two
  2-plane b3a8 surfaces and one WebKit 1125x1902 w30r.
- Health as experiment 8: 0 HANG, no real panic (same 3 benign strings), level 8 / 680 MHz accepted. Thermal max
  71.8 C (cpu-1-7).
- backboardd: 6578 frames (exp 8: 4981), late 146 = 2.2 %. Windows hz 67.9 (679 frames, gpu_ms 5.7) and
  **97.6 (489 frames, gpu_ms 4.4)**, then 42-55 sustained. Swap wait avg 6.3-6.5 ms, max 25.2 ms.
  The 97.6 Hz window refutes the "producer is locked to 60 Hz" hypothesis from experiment 8.
- New observation (display side): `SWEET FB` 600-frame windows as shown-fps / swaps-per-s / not-shown:
  45.9/47.8/25, 45.4/47.6/29, 45.7/47.0/17, 54.6/61.8/79, 55.8/69.6/149, 40.7/46.2/81. When swaps rise past
  60/s the shown rate stays at ~55-56 fps and `not shown` (all `replaced`) climbs. No window in any boot so far
  shows more than 60 fps. Hypothesis (NOT verified): the scanout path still flips at most once per ~16.7 ms
  (panel effectively at 60 Hz, or one flip per two TEs), so frames above 60/s are produced but replaced.
  Next: read the SWEET FB flip/TE pacing code and the panel-120 path (`fuxipanel120`, `fuxivsyncte`) to see what
  bounds shown frames.

## 2026-11-13 after experiment 9: what bounds shown frames (source + experiment-9 log), experiment 10 loader prepared

Read by the main agent in `E:\Codex-Xiaomi13-GPU-20261109\kernel-source-979` (the tree the device kernel is built from)
and checked against `native-surfrow2-lvl8-boot-1\boot.log`. Nothing booted, nothing written to the phone.

- The vsync notification is not the cap: `SWEET FB: vsync te-locked=600 fallback=0 ... period_us 8263` in every
  window after the panel switch (`SweetFramebuffer.cpp` `teVsyncTick`). CA is told 120 Hz and ticks at 121 Hz.
- The cap is the START cadence of the command-mode panel. `fuxi_gpu_scanout.inc` header comment of the spin section:
  the 1080x2400 transfer runs ~8.0 ms from the TE edge in an ~8.26 ms period, and a START is held by hardware until the
  next TE. The next START may only be written after the previous transfer is done (`fg_scanout_acquire`: floor, then
  CTL START clear and write line idle), so it catches the very next TE only if written in the ~0.3 ms between
  transfer-done and that TE. The display path polls at `period/16` = ~0.52 ms while TE is valid
  (`sweet_fb_rearm`), so it usually misses the gap and the frame goes out one period later.
- Experiment-9 evidence, `FUXI SCANOUT: waits per 600 starts ... gaps=a/b/c/d` (START-to-START in TE periods:
  1 / 2 / 3 / 4+): 90/189/195/125, 76/239/151/134, 97/128/195/180, 71/199/191/139, 55/176/209/160, 135/181/185/99,
  164/192/177/67, 122/204/179/95, 84/131/194/191. Single-period flips are 9-27 % of STARTs. In the busiest window
  (749 swaps in 10.76 s = 69.6/s) still only 164 of 600. All not-shown frames are `replaced` (unshown paths).
  This replaces the "flip at most once per ~16.7 ms" wording of the experiment-9 entry: single-period flips do
  happen, they are just rare because of the 0.3 ms window.
- The device kernel already contains the countermeasure, boot-arg only (`fuxi_gpu_abi_backend.inc:582`):
  bit 15 (32768) TE spin thread - wakes at the predicted transfer-done and STARTs inside the window; observed
  upstream (W176/W177/Y178 notes in the source). Bit 18 (262144, needs bit 15) hard spin - ~1 us polling with
  interrupts masked in 50 us slices, at most 2 ms per frame, and with triple buffering it may START the waiting
  buffer while a copy into another buffer is open. Upstream boot O220 ran with both
  (`fuxigpuncimport=2948672` = bits 6,9,10,11,12,13,14,15,18,19,21): docs-lanes/gpu-scanout.md:1424-1426 reports
  START gaps 62 % one TE, 33 % two, 5 % three or more, panel ~84 Hz. Not reproduced here.
  Not chosen: bit 16 (mid-period START; source says variants 0-3 failed on device, variant 4 unobserved, tear
  risk) and bit 17 (autorefresh; source says DO NOT ENABLE).
- Limit of this step: in the sustained windows of experiment 9 the producer itself only swaps 46-48 times/s
  (shown 45.4-45.9), so there the display is not the bottleneck. Spin can only help when frames arrive faster than
  ~60/s (the 62-70 swaps/s windows showed 54.6-55.8). Reaching 120 Hz still needs the producer side
  (render + present copy) inside 8.3 ms.
- Host tooling added: `prepare-args-on-loader.py <out> <source loader> <old=>new>...` (boot-args-only variant of any
  loader that has a source.txt; same kernel and ADT, Image identical outside the 608-byte args field) and
  `verify-args-on-loader-payload.py <out>`.
- Loader `native-surfrow2-spin-loader` (host only, NOT booted): source `native-surfrow2-lvl8-loader`, one token
  changed `fuxigpuncimport=9792` -> `304704` (bits 6,9,10,13,15,18). Kernel `239bf0d9...77b4` (surfrow2, unchanged),
  payload `4057ab965922e6d071b72fda07ab83dc22ee1740b14b49f49c32bd20f7bbfb8c`,
  boot.img `09c4d4bbdbd9e8b8f90bc96a119c37aaee866ea1f1906fe37d1afaaa5ba9e387`, args 542 chars.
  Before boot: map -> `verify-native-map.py` -> `verify-args-on-loader-payload.py native-surfrow2-spin-loader`;
  monitor `--kernel` = surfrow2 unstripped kernel. Read afterwards: `gaps=` first bucket, `FUXI SCANOUT: te_start
  spin`, `te_phase hit=`, `hard_spin`, `SWEET FB: stats` shown rate, `counted shown spin_started=`, HANG/panic,
  touch responsiveness (interrupt masking), thermal. Boot needs the user's approval in chat.

## 2026-11-13 experiment 10: TE spin + hard spin (fuxigpuncimport=304704) - RUN (user approved in chat; user NOT at the phone)
- Loader `native-surfrow2-spin-loader` (boot.img `09c4d4bb...e387`, kernel `239bf0d9...77b4`). Pre-boot map ->
  `verify-native-map.py` OK (inode 199819, 39 extents) -> `verify-args-on-loader-payload.py` OK -> unmap. Battery
  100 %, 30.9 C. Evidence `native-surfrow2-spin-boot-1\` (boot.log 4984 lines, sha256
  `13b77119207bda42fa19b3c7a959e192ec05d0bec58d9256acc2452d66cb21d3`), `native-surfrow2-spin-monitor-1.log`
  (ran the full 240 s; the traceback at its end is the final `display-after.bin` request after the device had
  already reset, `monitor_exit=0`). Afterwards Android `sys.boot_completed=1`, `mapped=false`. Nothing written.
- The user was away, so the only input was the built-in touch script (2 loops): 1079 backboardd frames in total
  (experiment 9 had 6578 with the user swiping). The script load produces only ~25 frames/s, so this boot says
  nothing about rates above 60 fps. NO visual verdict yet.
- Mechanism works as designed: `te_start spin wakes=277 published=227` / `264 / 223`; `te_phase hit=224 late=3`,
  `hit=222 late=1`, `period_us=8258..8261 done_us=7918..7920` (transfer time measured on this panel), phase error
  avg 10 us; `hard_spin waiting_at_done=227 started_in_window=226 done_to_start_us<50/<100/.../more=223/3/0/0/1`,
  `overrun=0`, `copy_open_but_started=6 / 8`, `cpu_spin_ms_per_s=2 / 18`.
- Same-workload comparison, touch-script loop 2 window (backboardd 24.2 vs 24.9 Hz in both boots):
  experiment 9 `gaps=84/131/194/191`, 600 shown in 21563 ms (27.8 fps), 650 swaps, 50 replaced;
  experiment 10 `gaps=158/213/85/144`, 600 shown in 18514 ms (32.4 fps), 628 swaps, 28 replaced.
  Single-period STARTs 14 % -> 26 %, one-or-two periods 36 % -> 62 %, three-period gaps 194 -> 85.
- Health: 0 HANG, no real panic (same 3 benign strings), level 8 / 680 MHz accepted, no scanout stop/stall line,
  `current_src0` match, late 73/1079 = 6.8 % (experiment 9 during its script loops: 72/1185 = 6.1 %).
  Thermal max 54.4 C (light load, not comparable with 71.8 C under manual swiping).
- Open: the >60 swaps/s case (does shown exceed ~56 fps?) and touch feel under interrupt masking need a boot with
  the user swiping by hand. Same loader can be reused (re-verify the mapping first).

## 2026-11-13 after experiment 10: producer side - the swap wait is the next serial stage; experiment 11 loader prepared

User: "繼續 目標是120". Analysis by the main agent from the experiment-9 log (manual swiping) and kernel-source-979.
Nothing booted for this section. A read-only survey of the remaining producer-side switches was delegated to
`antigravity/gemini-4-hydrogen --effort medium` (result not yet checked when this was written).

- Busiest experiment-9 window (boot.log:9792-9812, 679 swaps in 10.99 s): `SWEET FB: stages` ready_present 27 us,
  present_submit 48 us, submit_fence 718 us (the copy), fence_start 2280 us (waiting for the START gap - what
  experiment 10 attacks), start_start 18312 us. `swapwait ... intervals_1=314 intervals_2plus=350`: about half of
  the swaps follow the previous one by one TE period and half by two or more. backboardd: `gpu_ms` 4.4-5.5,
  `native swap wait avg_us` 6100-6500 (patch 0013), `gpu_busy_est_pct` 30-43. So the GPU is idle most of the time
  and a frame = CPU encode + a blocking ~6.3 ms wait; whenever that sum passes 8.26 ms the frame slips to the
  next tick (my reading of the 314/350 split; the CPU encode time itself is not in the log).
- Patch 0013's swap wait was added when `fuxigpuscanout` was OFF: the kernel then CPU-copied a swapped surface at
  once, before its render job ran (flicker, see the experiment-2 section). With `fuxigpuscanout=1` the present copy
  is a GPU job that is only issued with no client job in flight: `fuxi_gpu_scanout_reserve`
  (fuxi_gpu_scanout.inc:2343-2354) and `fuxi_gpu_copy` (fuxi_adreno.c:887-891) return 0 and set
  `fg_abi_present_wait` while `fg_client_busy`; `fg_client_busy` only clears when the fence equals `fg_abi_seq`
  (fuxi_gpu_abi_backend.inc:541-548), i.e. every queued job retired. `waitUntilScheduled` returns after
  `vkQueueSubmit` of the last batch (m2v-lf command_buffer/mod.rs:12-19, 2566-2588). So the copy of surface N
  cannot run before render job N finished, without the frontend blocking. Inference from source, NOT run:
  the Turnip KMD submit being synchronous is assumed from its `submit IBs n fence f` log, not read in source.
- Remaining risk without the wait: NativeMetal still takes no writer lease, so if CA re-renders surface N before
  copy N ran, copy N shows newer content (a skipped frame, not a back-jump); and the copy can be starved while
  client jobs keep queueing. Bit 12 (4096, `fg_abi_hold`): while a present waits, client submits are refused and
  the waiting present takes the idle GPU first, bounded to 4 ms (backend.inc:548, 845-848, 1029, 1041).
- Loader `native-surfrow2-nowait-loader` (host only, NOT booted): source `native-surfrow2-spin-loader`,
  `fuxigpuncimport=304704` -> `308800` (adds bit 12) and a new token `fuxiswapwait=0` (0013's documented off
  switch, read from kern.bootargs). Kernel unchanged `239bf0d9...77b4`,
  payload `63a1293d01b264e78c56fa7b2916c71ddc1af8d27289e3df2ab230c47fe1a2e4`,
  boot.img `7e00df09c003d4354f2276c01ef61bb3d4d78b621d69bc35bff2cd25c93cdd80`, args 557 chars.
  `prepare-args-on-loader.py` / `verify-args-on-loader-payload.py` now accept `+token` to append an argument.
  Before boot: map -> `verify-native-map.py` -> `verify-args-on-loader-payload.py native-surfrow2-nowait-loader`.
  Read afterwards: no `native swap wait` lines (wait off), backboardd `hz`, `intervals_1` vs `intervals_2plus`,
  `gaps=`, shown fps, `queue ... held / hold_expired / refused present`, `reuse_before_show`, HANG/panic.
  Flicker can only be judged by eye: the user must be at the phone and swipe by hand. Boot needs approval.

## 2026-11-13 producer-side switch survey (Gemini 4 Hydrogen, medium): received and checked

Report: `E:\Codex-Xiaomi13-GPU-20261109\perf-exp10\gemini-producer-survey.md` (67 turns, 174 s, read-only; input 125k,
cache read 1.16M tokens per `modelUsage`). Spot-checked by the main agent: gpu-scanout.md:962-965, 1246-1270, 1430,
1509-1524 and `fuxi_gpu_irq.inc:1-20` say what the report quotes. The rest of its table is NOT re-checked.
- Accepted facts: the highest shown rate upstream documents is 43.7 shown/s (gpu-scanout.md:963) and ~48/s inferred
  for O220 - this device already measured 55.8. Bit 12 hold applies on the classic (non-inline) path, which is what
  we run. Bit 26 (67108864): WaitFence sleeps on the GPU fence interrupt instead of the 100 us poll, with storm
  protection, "not yet observed". `fuxiearly` 1/2 change the KERNEL SwapWait(flags 0) return point, unobserved.
- NOT accepted: its recommendation 1 (inline bit 21 + park bit 27) - inline queues the copy when the userland writer
  lease ends and NativeMetal takes no lease; never run with NativeMetal; R223 with bit 16 got slower. Its
  recommendation 3 attributes the ~6.3 ms "native swap wait" to `fuxiearly`; that wait is patch 0013 in the frontend
  (`fuxiswapwait=0`), a different mechanism. `fuxigpuperf` / `fuxigpumerge` / `fuxigpusubmit` belong to upstream's
  FuxiMetal frontend; whether NativeMetal (`fuximetal=native`) reads them is unchecked.
- Order kept: experiment 11 (`fuxiswapwait=0` + bit 12) first; candidates after it, one at a time: bit 26 fence IRQ,
  `fuxiearly=1`, Stage 3 (needs the writer lease in NativeMetal first, see the 120 Hz survey section).

## 2026-11-13 two user-reported defects worked on while the user was away: Safari tab-overview purple, Settings > About

User (not at the phone): "先幫我修 設定關於本機 和 safari 切換分頁的背景發紫", with a photo
(`%USERPROFILE%\Downloads\IMG_0770.HEIC`): Safari's tab overview, the area behind the tab card is a smooth purple.
(The photo also shows vertical green/pink lines over the whole panel; the user did not mention them, not looked into.)
Nothing booted, nothing pushed, nothing written to the phone in this section.

### Purple: a new texture reads opaque magenta in the runtime (patch prepared, NOT built, NOT deployed)
- Host copy of SpringBoard's cached wallpaper blur
  (`texture-cache\Variant-home__CachedSnapshot-MediumGrayTintedBlur_tinted_2020_0_0_0.0_10_0.5_0.6.cpbitmap`, taken
  11-13 00:46): 96884 of 97913 ASTC blocks are void-extent blocks of ONE colour, RGB (179, 77, 179). The wallpaper
  itself (`0__RuntimeSnapshot-home.atx`) is blue, (15,138,203) and neighbours. (179,77,179)/255 = (0.70, 0.30, 0.70)
  = 0.4 x (1, 0, 1) + 0.6 x 0.5: opaque magenta under a 60 % mid-grey tint (the file name ends `_0.5_0.6`).
- Opaque magenta is what `metal2vulkan-runtime` gives a NEW `Private` texture that Metal would keep losslessly
  compressed: `texture/heap_tiled.rs:569-630` `fresh_pattern` (measured on Apple-silicon Macs), written by
  `Device::fresh_patterned` (`texture.rs:3486`, m2v pin 720ef1c). The bridge builds every descriptor from
  `TextureDescriptor::default()` (`native\bridge\NativeMetalObjects.rs` `texture_descriptor`), i.e.
  `allow_gpu_optimized_contents = true`, so every eligible new texture starts magenta.
- Reading: wherever CoreAnimation samples a texture nothing has written yet, magenta reaches the screen (Safari tab
  overview background) or a file (the cached blur). WHICH texture is read unwritten and why is NOT established;
  the same read on a real device presumably returns zero or never happens.
- Rejected fix: `allow_gpu_optimized_contents = false` in the bridge - it also adds `PIXEL_FORMAT_VIEW`
  (`MUTABLE_FORMAT`) to every image (`texture_descriptor.rs:326-337`) and changes compressed-blit/mipmap paths.
- Prepared fix, harness work tree only (branch `codex/astc-layout-backport`, uncommitted):
  `patches\metal2vulkan-fuxi-0002-fresh-zero.patch` (`fresh_patterned` returns `None` -> the texture starts
  zeroed/cleared like a format without a pattern; default off-pattern on iOS only, `FUXI_M2V_FRESH_PATTERN=1/0`
  overrides), one `cp` line each in `native-metal-check.yml` and `native-metal-host-tests.yml`,
  `docs\FRESH_ZERO.md`. `git apply --check` passes on the pinned texture.rs; 0001's texture.rs hunk is at line 5294,
  no overlap. NOT compiled: the host has rustc 1.97.1, the crate needs 1.98 (a local `cargo check` attempt only
  rewrote the git-ignored `m2v-lf\Cargo.lock`). Needs: commit + push (user approval) -> iOS CI -> light deploy
  (image write, user approval) -> boot.
- Already-written magenta (the cached blur file on the phone) will not change until SpringBoard regenerates it.

### About: upstream documents the same defect and a diagnostic; loader prepared (NOT booted)
- `kernel-source-979\docs-lanes\wallpaper.md:371-395`: page is `PSGAboutController` / `PSGAboutDataSource
  _loadValues`; the Bluetooth address comes from the device tree, not bluetoothd; upstream's guess is a synchronous
  XPC wait (media/photos counts, CoreTelephony, softwareupdate, fairplayd). Unresolved upstream.
- Process sampler (`xnu\patches\0054-sweet-process-sampler.patch`): prints `SMP name[pid] tN pc lr bt` for every
  thread of the named processes, first at 90 s then every 120 s. On FUXI it only starts when `sweetsample=` is in
  the boot args (`fuxi-unixnu\sweet_ufs.c:944-947`) - none of our boots had it (0 `SMP` lines in every log).
- Loader `native-surfrow2-aboutdiag-loader` (host only): source `native-surfrow2-spin-loader`; `sweetnull` gains
  `,Preferences`; `fuxitouchscript=150` -> `3000` (so the scripted swipes do not leave the page);
  added `sweetsample=Preferences sweetiolog=Preferences`. args 602 of 607. payload
  `dd58f94b9c52d198e507ab92aeaf4e7a543612bce8ce81e6ad7ae6bb0b058ef6`, boot.img
  `c5fae4d95c99041005b7d7f32be32e2dfa18ba29687437402e3560b3238e26c0`.
  Run with the user at the phone: open Settings > General > About within ~3 minutes and leave it open; monitor
  `--seconds 480 --reset-minutes 8` to get samples at 90/210/330/450 s. Then symbolicate thread 0 of
  `SMP Preferences` (`tools/sweet-crash.py`, address minus slide) and read the `IOK Preferences:` lookups.

## 2026-11-13 fresh-zero (metal2vulkan patch 0002): pushed, iOS CI green, artifact verified, host image staged

User approved the push ("好" to "commit 並 push ... 讓 CI 建置").
- Harness commit `922c9800c5c35cdf566f9e512a24e845a782d581` on `codex/astc-layout-backport` (pushed
  `17fbbec..922c980`; author identity reused from the previous commit, no git identity on this machine).
- iOS build run 43417101386: success (first compile of the patch). Host integration run 43417105334: success
  (job `native-host`, completed 2026-11-13T11:50:21Z, checked with `gh run view` in the next session).
- Artifact 14033525251, zip sha256 `fa6f173cc5d9d4e7767a6904dfef619265881980303ce06069e5e7bfa8e1795a`, checked by
  `xiaomi13-layout-backport-20261112\verify-ci-artifact-0014.py` -> `artifact-verification-0014.json`,
  `ios-artifact-0014\`: digest matches GitHub, 19 manifest files, no extras, three reviewed sources identical.
  `FUXI_M2V_FRESH_PATTERN` occurs only in `libnative_metal_bridge.dylib`
  (`fd9a30a198507541a786de529053f14edf48515b1bb0ef38a566fe7cb7c673d4`, was `5e304a3d...`);
  FuxiNativeMetal is unchanged (`413439f7...`).
- Staging on the host only: `native-layout0014-overlay.qcow2` over `native-layout0013-root.img` (`b5c10b14...`,
  re-hashed), `prepare-layout0014-guest.py`, `native-layout0014-stage.log`: guest_exit=0, fsck OK three times,
  `INSPECT_SUCCESS`, staged bridge hash `fd9a30a1...`. Export `native-layout0014-root.img`, 21474836480 bytes,
  sha256 `ef711179d91460b230c101db833692a3f47a5ab2479b29033441d073c88c129c` (`.sha256` file written).
- NOT done: transfer to the phone, guarded write, readback, boot. All need the user's approval. Note the image is
  built on the CLEAN 0013 image: writing it replaces the phone's current iOS state (logs, caches, and the cached
  magenta wallpaper blur, which would then be regenerated on first boot). Light flow per the deploy memory:
  map -> `verify-native-map.py` -> `delta-stage-hash.py` -> guarded writer -> readback -> boot.

## 2026-11-13 evening: 0014 deployed; aboutdiag boot; experiment 11 boot (user approved "部署＋兩次開機")

### Deploy 0014 (light flow, all under `E:\Codex-Xiaomi13-GPU-20261109\`)
- map -> `verify-native-map.py` OK (inode 199819, 39 extents, dm-8).
- `delta-stage-hash.py --name layout0014` -> `native-layout0014-transfer.log`: 419 differing 4 MiB blocks (1676 MiB),
  delta sha256 `80de198d...39af`, `STAGE_VERIFIED /data/local/tmp/codex-layout0014-stage.img ef711179...129c`.
- Phone image before the write: sha256 `762eba8fd8be67ef25f3aacf4504487b3965234e5890a5978876d30069f69dc3`
  (0013 + the state of the day's boots; NOT kept anywhere, it was overwritten).
- `native-layout0014-write.sh` (sha256 `a486346f...f9b0`, 0013's writer with the stage path changed) run by
  `native-layout0014-supervisor.sh` (`0606dec9...1770`), control dir `/data/local/tmp/codex-layout0014-write-control`:
  status 0, 21474836480 bytes in 22.45 s. unmap/remap -> mapper sha256 `ef711179...129c` = READBACK_MATCH.
  Stage file removed from the phone afterwards.

### aboutdiag boot (20:36:15, `native-surfrow2-aboutdiag-loader`, monitor 480 s / reset 8 min, user at the phone)
- Evidence `native-layout0014-aboutdiag-boot-1\` (boot.log 16109 lines, sha256 `245cfce3...cfc6`),
  `native-layout0014-aboutdiag-monitor-1.log` (UTF-16 with a garbled last line: the runner appended UTF-8 to a
  UTF-16 file; the traceback at the end is the usual device-left one). No HANG, no panic; crashes only
  bluetoothd/fairplayd.H2 x45 (`sweetnoexec`), driverkitd x1, assetsd x3. Afterwards Android, `mapped=false`.
- User, during the boot: "關於本機還是打不開 瀏覽器分頁切換背景依舊紫色 流暢度感知不明顯", "但肯定沒有超過60貞",
  "關閉螢幕還會閃一下SAFARI切換分頁的發紫頁面", and a few minutes later "關於本機突然進去了".
- **About: cause found.** Sampler printed 3 samples (lines 4387, 8462, 13690; Preferences pid 266 and 312, not
  running at sample 2). Symbolicated against the host dyld cache with a new scratchpad tool `dsc_sym.py`
  (parses the split cache + `.symbols`; no `ipsw` on this host). In both samples one thread sits in
  `-[PSUIPrefsListController updateEthernetWithCompletion:]` -> `_SCNetworkInterfaceCopyAllWithPreferences` ->
  `dispatch_once` -> `__wait_for_IOKit_to_quiesce` block -> `SCDynamicStoreNotifyWait` -> `mach_msg`.
  In sample 0 the MAIN thread is in GeneralSettingsUI -> `_SCNetworkInterfaceCopyAllWithPreferences` ->
  `_dispatch_once_wait` -> `__ulock_wait`, i.e. blocked behind that same once. So About waits for configd's
  InterfaceNamer to publish "IOKit is quiet" (dynamic store key `Plugin:InterfaceNamer`).
- configd in the image contains the strings `WaitStackTimeout`, `WaitQuietTimeout`, `IOBusyInterest`,
  `IONetworkStack` (raw scan of `native-layout0014-root.img`, offsets 0x2017f23a7 / 0x34954af37). From memory of
  the open-source configd (NOT re-read here): InterfaceNamer waits for IONetworkStack (default 300 s) and for the
  IOKit registry to go quiet (default 240 s), both overridable by those two Info.plist keys, and publishes a
  timeout key that `__wait_for_IOKit_to_quiesce` also accepts. That fits "it suddenly opened after minutes".
  Which of the two waits expires here, and why IOKit never reports quiet, is NOT established.
- **Purple: the fix was never exercised.** The 846x1827 `FUXI LINEAR: foreign snapshot imported format=204` lines
  right after `exec MobileSafari` (boot.log 3910-3994) are exactly the size of SpringBoard's cached wallpaper blur
  (`Variant-home__CachedSnapshot-MediumGrayTintedBlur...cpbitmap`, 96884 blocks x 16 = 1550144 bytes), and that
  file, magenta content included, is inside the "clean" host image: its first 4 KiB match at image offset
  0x38aaef000 followed by 378 4-KiB runs of the magenta void-extent block (scratchpad `find_blur.py`). So 0014
  still shows the OLD cached file; SpringBoard did not regenerate it. Whether a regenerated blur is correct with
  fresh-zero is unknown until the cached file is removed from the image. Note for the new observation
  (purple page flashes when the screen is turned off): not analysed.
- Frame rate in this boot (spin config, user using Settings/Safari, not a swipe test): backboardd windows of
  >=100 frames peak at 66.9 Hz once, otherwise <= 54.3 Hz. Matches the user's "definitely not above 60".

### Experiment 11 (20:46:00, `native-surfrow2-nowait-loader`: `fuxiswapwait=0` + bit 12, 308800), 240 s
- Pre-boot map -> `verify-native-map.py` OK -> `verify-args-on-loader-payload.py` OK -> unmap. Evidence
  `native-layout0014-nowait-boot-1\` (boot.log 5244 lines, sha256 `e7038f4a...73ac`),
  `native-layout0014-nowait-monitor-1.log`. No HANG, no panic, `reuse_before_show=0`, crashes: the usual
  bluetoothd/fairplayd x21, driverkitd x1, assetsd x2, mediaserverd x1 (not looked at). Android afterwards,
  `mapped=false`, battery 98 %, 40.5 C.
- 0 `native swap wait` lines (the wait is off). `queue ... held 132 hold_expired 9 refused present 0`.
- The user hardly touched the screen: 22 touch-downs including the script's, 66 backboardd frames before the
  touch script started (line 3244). So again NO manual-swipe data and NO visual verdict on flicker yet.
- Same-workload comparison, touch-script loop 2: experiment 9 600 shown in 21563 ms (27.8 fps); experiment 10
  18514 ms (32.4 fps), `gaps=158/213/85/144`; experiment 11 13026 ms (46.1 fps), `gaps=217/233/61/89`, 620 swaps,
  20 not shown. backboardd window in loop 2: 33.7 Hz (337 frames) against 24.9 Hz in experiment 10.
  `late` 112/1206 = 9.3 % (experiment 10: 6.8 %).
- Open: flicker and feel by eye under manual swiping; the >60 swaps/s case. Same loader can be reused.

## 2026-11-13 21:10: 0015 host image staged (user: "備好 0015"); the purple reading above is CORRECTED

Host only. Nothing transferred, written or booted.

### What 0015 is
`native-layout0015-root.img` = 0014 with two edits made in the Alpine guest (`guest-run.py`, a parameterised copy of
`prepare-layout0014-guest.py`; share `native-layout0015-share\`, script `stage-0015.sh`, log
`native-layout0015-stage.log`): fsck OK before and after, `STAGE0015_SUCCESS`, `qemu-img check` clean.
- `/System/Library/SystemConfiguration/InterfaceNamer.bundle/Info.plist` (binary plist, was 843 bytes, sha256
  `06654ce2...c7a9`): added `WaitStackTimeout = 15.0` and `WaitQuietTimeout = 15.0` (`plist-edit.py`; now 900 bytes,
  sha256 `7040cb4e...7f8d`; every other key unchanged, owner/mode kept). The bundle has `Builtin = True` and a
  `_CodeSignature` whose resource seal no longer matches; nothing on this port checks it (assumption: the boot log
  says "trust caches unavailable (Image4/AMFI not installed)"; CFBundle does not verify seals).
- Removed the one cached blur file
  `/private/var/containers/Data/System/4FADF948-.../Library/Caches/com.apple.PaperBoardUI/PosterViewController-0x116041400-11043D53-.../MappedImageCache/Variant-home/CachedSnapshot-MediumGrayTintedBlur_tinted_2020:0:0:0.0:10:0.5:0.6.cpbitmap`
  (sha256 `c3741559...7011`, identical to the host copy in `texture-cache\`).
- Export: 21474836480 bytes, sha256 `ddcc87181f1a78c0fd282d0cd68aa0714f8b1223f93f7d9e84d7fe12f1401677`
  (`.sha256` written). Differs from 0014 in 8 of 5120 4-MiB blocks (0, 179, 1719-1723, 1904), so the delta transfer
  will be about 32 MiB.
- Deploy when approved (light flow): map -> `verify-native-map.py` -> `delta-stage-hash.py --name layout0015` ->
  writer/supervisor copied from the 0014 pair (stage path, three hashes; the phone's CURRENT sha must be measured
  first, it is 0014 + two boots) -> readback -> boot.

### About: checked against the configd source (apple-oss-distributions/configd, main; scratchpad copy, not kept)
- `Plugins/InterfaceNamer/ifnamer.c`: `WAIT_STACK_TIMEOUT_DEFAULT 300.0`, `WAIT_QUIET_TIMEOUT_DEFAULT 240.0`
  (:114-118); both read from the bundle Info.plist with `CFBundleGetInfoDictionary` and must be CFNumbers > 0
  (:4533-4549); the timer starts with the stack timeout (:4387-4389) and is shortened to the quiet timeout only
  once IONetworkStack appears (:4122-4128); `timerCallback` publishes `kInterfaceNamerKey_Timeout` (:4318).
- `SystemConfiguration.fproj/SCNetworkInterface.c:4911-4955`: `__wait_for_IOKit_to_quiesce` loops until the
  `Plugin:InterfaceNamer` value contains Quiet, Complete (iOS) or Timeout.
- So on this phone About becomes reachable about 300 s after configd starts (IONetworkStack never appears or
  IOKit never goes quiet - which one is NOT established), which is longer than every 4-minute boot and matches the
  user getting in after about 7 minutes. With 0015 it should be about 15 s. This is a workaround: why IOKit never
  reports quiet is still unknown. The source is the current open-source tree, not necessarily the iOS 16.7 build
  on the phone; the two key names and "IOBusyInterest"/"IONetworkStack" are present in the phone's configd binary.

### Purple: CORRECTION of "the fix was never exercised"
- The PaperBoardUI cache holds TEN `PosterViewController-<pointer>-<UUID>` directories (one per boot of the image
  lineage); only one contains a file, the others have empty `Variant-home`/`Variant-lock`. So the cached file is an
  orphan nobody reads: each boot creates a new directory.
- The 846x1827 import seen every boot has `length=1556480` = the 1550144-byte ASTC payload rounded to 16 KiB
  pages, not the 1566614-byte file mapped. So SpringBoard regenerates the blur in memory at every boot.
- Therefore 0014's fresh-zero WAS in effect while the user still saw purple. The most common 16-byte block of the
  old file is a VALID void-extent block encoding (179,77,179), i.e. SpringBoard was handed an image that was
  already uniformly purple and encoded it. Where that image gets its magenta is open again. Removing the orphan
  file (done in 0015) is harmless but is not expected to change anything.
- Untested idea (mine): an ASTC texture that never receives its data reads magenta both before the patch (fresh
  pattern) and after it (all-zero ASTC blocks decode to the ASTC error colour, opaque magenta). No refusal line
  (`native-metal:`) appears in the aboutdiag log. A read-only source survey of the remaining paths is delegated
  (`perf-exp10\gemini-task-magenta-paths.md`, Gemini 4 Hydrogen medium).

### Magenta-path survey (Gemini 4 Hydrogen, medium): received and spot-checked
Report `perf-exp10\gemini-magenta-paths.md` (56 turns, 239 s, read-only; input 125k, cache read 1.04M tokens per
`modelUsage`). Checked by the main agent: `heap_tiled.rs:569-658` (`fresh_pattern` is `None` for block-compressed
formats, comment :577-578), `texture.rs:3452-3472` (pattern only for `private && !held`, otherwise zero copy or
clear), `texture.rs:3699-3713` (`clears_to_zero` excludes block-compressed formats), `clear_reach.rs:176-230`
(a `DontCare` load becomes `Load` or a clear to the format's zero, no debug colour). The rest is NOT re-checked.
- With patch 0002 the only magenta constant left in the runtime is unreachable (`heap_tiled.rs:583`, read only by
  `fresh_patterned`). `degrade_fresh_pattern` only emits a notice. No ASTC software decode exists (`BlockDecode`
  covers BC1-BC5 only), IOSurface-backed and wrapped `VkImage` textures take no pattern.
- A new ASTC texture is filled with zero BYTES at every level. An all-zero ASTC block has a reserved block mode, so
  a decoder returns the ASTC error colour, opaque magenta - same colour as the fresh pattern, and patch 0002 does not
  touch it. This is the one remaining candidate: some ASTC texture in backboardd is sampled without ever having
  received its blocks. Inference (the ASTC rule is from the spec, the Adreno decode result is NOT measured here),
  and WHICH texture is not known. The wallpaper `.atx` has a single level (2764200 - 2747808 = 16392 header bytes),
  so "unwritten mip levels of the wallpaper" is not it.
- Next evidence (not built): a bridge/runtime diagnostic that reports the first sample or blit read of an ASTC
  texture whose first-move is still the zero fill (format, size, usage, caller), boot once, open Safari's tab
  overview. Needs a harness commit + push + iOS CI + image deploy.

## 2026-11-13 21:12-21:27: 0015 deployed and booted once (user: "部署並開機"); written up in the NEXT session

The session that ran this hit its usage limit at 21:28 before recording anything. Reconstructed from its transcript
(`.claude\projects\D--13\6ba35c40-...jsonl`) and the files on E:. The phone's current root is 0015.

### Deploy (light flow)
- map -> `verify-native-map.py` OK (inode 199819, 39 extents, dm-8). `native-layout0015-transfer.log`: 195 differing
  blocks (780 MiB; 0014 + two boots of state, not just the 8 image blocks), `STAGE_VERIFIED ... ddcc8718...1677`.
- Phone image before the write: sha256 `138d59e0ca2d73477a74d4f482d069d71eed79c2131d03ceafbbaf9bea0c852e` (not kept).
- `native-layout0015-write.sh` (`a2f970b9...d593`) via `native-layout0015-supervisor.sh` (`de3d885c...5cc1`): status 0,
  21474836480 bytes in 24.1 s; unmap/remap -> readback `ddcc8718...1677` = READBACK_MATCH. The stage image is gone
  from the phone; the two scripts and the control dir are still in `/data/local/tmp`.

### Boot (21:22:33, `native-surfrow2-nowait-loader` = experiment 11 config, 240 s, user at the phone)
- Evidence `native-layout0015-nowait-boot-1\` (boot.log 197844 lines, sha256 `066d580d...4576`),
  `native-layout0015-nowait-monitor-1.log` (usual device-left traceback).
- User: "safari尚未修復 關於本機可以進入了 流暢度依舊", then asked whether the CPU has any scheduling / power
  management, because opening an app is slow.
- **About: fixed by the 15 s InterfaceNamer timeouts** (user verdict). Root cause (IOKit never quiet) still open.
- Purple: unchanged, as expected.
- **NEW: backboardd lost the Vulkan device at about 161 s** (boot.log 11249, right after `submit IBs 88 fence 5544`):
  `FUXI MTL: backboardd gate runtime-failure next-respawn=CPU persisted=1`, then 53103 `commandBuffer refused:
  command buffer: DeviceLost` lines (plus 5704 texture, 3029 no-copy buffer). The same backboardd pid kept running
  until it was re-executed at line 197081 with `gate deny reason=strikes strikes=1` (CPU path). No kernel-side error
  near it (no nonzero `FUXI ABI ... rc`, no `TU:` line other than submits, `stuck 0 stall kicks 0`), and the reason
  string of the first refusal is empty. **Cause NOT established.** IB count is not it (88 here; 123 passed in
  experiments 10 and 11). No other boot of the day has a DeviceLost line (checked: 0014-nowait, 0014-aboutdiag,
  spin, lvl8). This is the second boot with `fuxiswapwait=0` and the first with real manual use, so the no-wait mode
  is NOT a baseline candidate until this is understood. Candidate sites in `turnip-darwin-kmd.patch` (original
  patch text, the deployed queue variant may differ): `submit failed`, `GPU did not retire a job` (5 s),
  `no free job slot` (64 attempts), `wait before submit failed`, `GPU faulted or hung`, the deferred-BO paths.
  What the user saw for the last ~70 s is not known (they did not mention a freeze).
- Shown frame rate before the loss, with the user using the phone (139 touch-downs): 600-frame windows of 41.1,
  **57.4**, 48.5, 45.9, 32.6, 45.3 fps; single-period starts up to 257/600 = 43 %. Still never above 60.
- Hottest sensor 78.9 C at 130 s (cpu-1-1), 77.7 C at 220 s. Nothing limits it (see below).

### CPU frequency / scheduling / power management: what is actually on (answer to the user's question)
- None of the day's loaders pass `fuxiperf`, `fuxicores`, `fuxitp` or `fuxicpuload`; the boot log has no
  `FUXI PERF` / `FUXI IDLE` line. So XNU runs **no frequency governor, no thermal guard and no core
  recommendation**. Both exist in the running kernel (strings present in `kernel-surfrow2-artifact`; source
  `kernel-source-979\xnu\fuxi-perf\fuxi_perf.c`, patch 0097; idle log patch 0105).
- `thermal.jsonl` (host monitor, every 10 s): silver always 1670.4 MHz; gold reads 499.2 or 2707.2 MHz, prime
  595.2 or 2956.8 MHz, and the vote register flips with it (index 0 <-> 19/20). Who flips it without the governor
  is NOT established (not looked at).
- Upstream `docs-lanes\cooling.md:432-441` OBSERVED the consequence of no recommendation mask: the clutch
  scheduler is homogeneous here and put most UI load on logical CPUs 0-1 (Cortex-A510) while CoreAnimation made
  5-20 fps; `fuxicores=0x7c` keeps normal threads on gold+prime. Whether that is what limits backboardd and app
  launch on THIS phone is a hypothesis (no per-CPU data yet); `fuxicpuload=10` gives that data.
- The governor's price: it holds gold/prime at "cool" (about 1.4 GHz) unless there was input within 1.5 s AND the
  hottest CPU sensor is below the guard (start <= 60 C default, hot 62 C, limit 68 C, `fp_arg` ranges in
  `fuxi_perf.c:528-540`). With `fuxigpulevel=8` the phone sits at 60-79 C, so boosts may be refused much of the time.

### Experiment 12 loader prepared (host only, NOT booted, not approved)
`native-surfrow2-perf-loader` = `native-surfrow2-spin-loader` (304704, swap wait ON) + `fuxiperf=1 fuxicores=0x7c
fuxicpuload=10` (`prepare-args-on-loader.py`; payload `90361c16...a1f5`, boot.img `29be7133...2819`, same kernel
`239bf0d9...77b4`). Read from the log afterwards: `FUXI PERF: ready/stats` (boost time, max temp, refusals),
`FUXI PERF: recommended cores 0x7c`, the `FUXI IDLE` per-CPU lines, frame windows, app-launch feel by eye.
If boosts are mostly refused for heat, the follow-up is the same args with a lower `fuxigpulevel`.

## 2026-11-13 experiment 12: governor + recommended cores + per-CPU load - RUN (user: "開機實驗 12")

- `verify-args-on-loader-payload.py` refused the loader: its out-of-scope list contains `fuxiperf=`. Changed the
  check to skip tokens that `swaps.txt` lists with `+` (the experiment's own subject); every other check unchanged.
  The spin loader still verifies with the edited script.
- map -> `verify-native-map.py` OK (inode 199819, 39 extents) -> payload verify OK -> unmap. Boot 21:37:02,
  boot.img `29be7133...2819`, 240 s, user at the phone. Evidence `native-layout0015-perf-boot-1\` (boot.log 11590
  lines, sha256 `eaa013e4...ba97`), `native-layout0015-perf-monitor-1.log`. Afterwards Android, `mapped=false`,
  battery 100 %, 38.1 C.
- No panic, no HANG, **no DeviceLost**, backboardd gate never denied. Crashes: bluetoothd/fairplayd x21 each,
  driverkitd x1, assetsd x2, AegirPoster x2 (not looked at). `peak_bos 1018`.
- Governor: `FUXI PERF: recommended cores 0x7c`, `ready, LUT cool 1228800/1401600/1363200 kHz high
  1228800/2323200/2342400 kHz` (measured unboosted gold 1401 / prime 1708 MHz, boosted up to 2323 / 2592).
  During use each 10 s window has 7-19 boosts, **only 0.7-1.65 s boosted**, and `cuts` equals `boosts`; time above
  62 C is 0-90 ms per window, max 65.9 C. So boosts are ended by the duty budget (`fp_budget_ms = 200` per 1000 ms,
  `fuxi_perf.c:56`, arg `fuxiperfbudget` 20..500), not mainly by heat (my reading of the counters; the cut reason is
  not logged per event). Net effect: gold sits at 1.4 GHz about 85 % of the time, against 2.7 GHz readings before.
- Mask works: logical c0/c1 (A510) 0 % in every 10 s line; c2-c5 (gold) 50-75 % during use (avg 33-51 %), 13-49 %
  in the quieter windows; **c6 (prime X3) only 0-13 %**. So with the mask the four gold cores are heavily loaded
  even at rest. Which processes burn it is NOT measured (fuxicpuload has no per-process split).
- Shown frames (155 touch-downs, user opened Settings, Safari, Photos): 600-frame windows of 41.4, 44.4, 46.1,
  **49.9**, 45.0 fps during use; single-period starts 10-175 of 600 (2-29 %). Lower than the 0015 no-wait boot
  (best 57.4 fps, 43 %) and about the same as aboutdiag (spin, no governor). No gain in frame rate.
- Host TSENS samples max 58.8 C (governor's own max 65.9 C) against 78.9 C without the governor.
- User's visual verdict: "流暢度應該有提升 但我肉眼看不確定" (uncertain, leaning positive; nothing said about app
  launch delay). The frame counts do not show a gain, so if it is real it is evenness, not rate (my guess).
- Experiment 13 loader prepared on the host, NOT booted: `native-surfrow2-perf500-loader` = perf loader +
  `fuxiperfbudget=500` (the arg's maximum; boost duty 20 % -> 50 %).
- Experiment 13 RUN (user: "開機實驗 13"), 21:46:10, 240 s, user at the phone. The verifier refused
  `native-surfrow2-perf500-loader` (derived from the perf loader, so `fuxiperf=1` is inherited, not a `+` line);
  rebuilt as `native-surfrow2-perf500b-loader` straight from the spin loader with the four `+` tokens: byte-identical
  payload `c8fa77ab...75b3`, boot.img `7e266046...5dc6`, verified, booted. Evidence
  `native-layout0015-perf500-boot-1\` (boot.log 15043 lines, sha256 `73626941...3a21`),
  `native-layout0015-perf500-monitor-1.log`. Afterwards Android, `mapped=false`, 100 %, 40.0 C.
  - No panic, HANG or DeviceLost; crashes bluetoothd/fairplayd x21, assetsd x5, driverkitd x1. `peak_bos 1072`.
  - `duty 500/1000 ms` accepted. Boosted time per 10 s window: 0-2.76 s (several windows 0 boosts while max temp
    62.3-64.7 C). With the budget raised, heat (62 C hot / 60 C start) is now what refuses boosts. Host TSENS max
    62.3 C.
  - Load as before: c0/c1 0 %, gold 50-79 % in use.
  - Shown frames (217 touch-downs; Settings, Safari, Phone, Messages, App Store, Photos): 600-frame windows of 51.7,
    46.4, 51.4, 57.6, 45.4, 39.0, 42.8, **63.9**, 45.0, 41.9 fps. 63.9 fps (600 in 9397 ms, single-period starts
    399/600 = 66 %) is the first shown-frame window above 60 in any boot; the 57.6 window has 312/600.
  - User's visual verdict: "比較順" (smoother than experiment 12). So `native-surfrow2-perf500b-loader` is the new
    baseline loader.
- Who loads the gold cores, first lead (from the `FUXI IDLE: ipiwake` lines of experiments 12 AND 13): in every
  10 s window `vmd[222]>vmd[222]` 2500-4000 wakes, `vmd>launchd` 1200-1750, `launchd>kernel_task` 1100-1900,
  `launchd>launchd` 800-1700, against `rest` 4500-7500. vmd (the voicemail daemon) is exec'd once (boot.log 2025),
  never crashes and prints nothing, so it sits in a steady loop with launchd at roughly 150 round trips per second.
  These are wake counts, NOT CPU time; how much of the 50-79 % it explains is unmeasured. Upstream docs do not
  mention vmd.
- Experiment 14 loader prepared on the host, NOT booted: `native-surfrow2-novmd-loader` = spin loader + the four
  experiment-13 tokens + `sweetnoexec=bluetoothd,fairplayd.H2,vmd` (patch 0095: the exec is blocked, launchd
  retries about every 10 s like the other two). payload `04291193...d416`, boot.img `f8ac87ee...a630`. The boot-args
  field is now 606 of 607 usable characters: the next added token needs another one removed.
- Experiment 14 RUN (user: "開機實驗 14"), 21:54:30, 240 s, user at the phone. Evidence
  `native-layout0015-novmd-boot-1\` (boot.log 323311 lines, sha256 `c0624261...69a4`),
  `native-layout0015-novmd-monitor-1.log`. Afterwards Android, `mapped=false`, 99 %, 40.8 C.
  - `exec vmd blocked` (launchd retries: `CRASH vmd` x20, like bluetoothd/fairplayd). The vmd/launchd wake loop is
    gone: top wakers are now backboardd, kernel_task, SpringBoard, runningboardd (hundreds per 10 s, not thousands).
  - Load: the quiet window at up 68 s is **avg 2 %** (gold 2-5 %) against 22 % (gold 15-48 %) in experiments 12/13.
    In use gold is 23-70 % and **prime now carries 12-43 %** (was 0-13 %). So vmd's loop was a real CPU cost.
  - Boosted time per 10 s window 0-3.06 s; governor max 66.7 C, host TSENS max 61.5 C. Heat still refuses boosts
    in some windows (one with 4.2 s above 62 C).
  - Shown frames before the loss (208 touch-downs in the whole boot, Settings only): 600-frame windows of 49.1,
    45.0, 59.6, 45.0, **67.4** (600 in 8897 ms, 319/600 single-period), 38.4, 31.5, 58.1, 56.5, 49.2 fps.
  - **DeviceLost AGAIN**, boot.log 13392, about 200 s up (log clock 16:03:21; the first one was 16:03:20 in the
    0015 no-wait boot), same shape: `gate runtime-failure next-respawn=CPU persisted=1`, empty first refusal reason,
    102639 DeviceLost lines, backboardd re-exec'd on the CPU path at line 322177. This boot has the swap wait ON, so
    **the no-wait mode is not the cause** (correction of the suspicion above). Both losses come 500-1000 log lines
    after `FUXI TOUCH SCRIPT: loop 1 start` with the user touching at the same time; experiments 12 and 13 passed
    the same point under the same conditions. No kernel-side error line before either. Last submits before this
    one are ordinary (3-7 IBs; a 127-IB submit 15 fences earlier). Also new in this boot: AegirPoster x16 and
    PosterBoard x7 crashes (sig 6), not looked at. Cause still NOT established; it needs the reason string of
    `vk_device_set_lost` to reach the log (runtime/bridge diagnostic build).
  - User: "有異常", and "我在更換壁紙時 腳本向上滑動回桌面就卡住". So the screen froze when the touch script swiped
    home while the user was in the wallpaper picker. Log agrees: AegirPoster exec+crash pairs at 12269-12375 and
    13123-13155, loss at 13392. The first lost boot has the same order (Preferences exec 8950, AegirPoster crash
    pairs 9978-10004, loss 11249). Experiments 12/13 had AegirPoster crashes early (around line 3100) and no loss,
    so the crash alone is not sufficient. Working scenario (hypothesis): wallpaper picker open -> leave to the home
    screen -> DeviceLost. No smoothness verdict for this boot.
  - Read-only source survey delegated (`perf-exp10\gemini-task-device-lost.md`, Gemini 4 Hydrogen medium; report
    to `perf-exp10\gemini-device-lost.md`).
- User, later: "對卡住之後好像gpu驅動就掉了" (matches the gate's CPU respawn).
- DeviceLost survey received (`perf-exp10\gemini-device-lost.md`, 87 turns, 344 s; input 161k, cache read 2.48M
  tokens per `modelUsage`). Checked by the main agent: `NativeBridgeMetalFrontend.mm:1080-1083` (the completion
  callback passes `text` unchanged when `nmb_command_buffer_error` returns 0 -> the EMPTY first refusal),
  `completion.rs:566-616` (`failure()` marks the device lost on `ERROR_DEVICE_LOST` from `vkWaitForFences` /
  `vkWaitSemaphores` / counter read, prints nothing; a TIMEOUT is not a loss), `NativeMetalObjects.rs:188-210`
  (`observe_message` keeps warnings/errors as a count and `LAST_WARNING`, prints nothing). NOT re-checked: the rest,
  and the Mesa claim (vk_device.c is not on this host) that `vk_device_set_lost` goes to the debug-utils messenger
  rather than stderr once a messenger is registered.
  - So both losses were first seen by a command buffer's COMPLETION (a wait call returned DEVICE_LOST), i.e. Turnip
    set the device lost; its reason string was swallowed. Candidates on the wait path in `turnip-darwin-kmd.patch`:
    `dw_status_to_vk` "%s failed: 0x%x" (:444) and `darwin_device_check_status` "GPU faulted or hung" (:1256).
  - Extra from the main agent: the novmd boot's kernel line before the loss shows `queue ... full 20` (boot.log
    12982), the only nonzero `full` in any boot checked (0015-nowait, perf, perf500, aboutdiag all 0). `full` =
    `FG_ABI_QUEUE_MAX` jobs in flight or no ring room (0098 patch :5088), a busy refusal, not an error. `peak_bos`
    climbed 1111 -> 1644 -> 1823 in that boot (wallpaper picker). Kernel counters stay `stuck 0 stall kicks 0` and
    all 7354 submits were accepted. Not a cause by itself; noted.
- Diagnostic prepared in the harness work tree, branch `codex/astc-layout-backport`, **uncommitted, not pushed, not
  compiled** (no local toolchain): `patches\nativemetal-fuxi-0014-lost-reason.patch` (made by diffing scratch
  copies of the stack-top files; `git apply --check` passes on the `native\` work tree) + one `cp` line in each of
  `native-metal-check.yml` / `native-metal-host-tests.yml`. It (1) prints the first 32 messenger messages of
  severity Error as `FUXI NMB VK ERROR: [id] text` and (2) replaces the empty completion reason with a fixed text
  plus `nmb_last_error()`. Bridge + frontend only; the runtime and Mesa are untouched. If Mesa does not route the
  lost reason through the messenger this shows nothing new and the next step is a runtime patch
  (`device/submit.rs:311`, `completion.rs:608`). Needs: commit + push (user approval) -> iOS CI -> image 0016 ->
  light deploy -> boot with the wallpaper-picker scenario.
- User approved ("commit 並 push"): harness commit `ca5ab97` on `codex/astc-layout-backport`, pushed
  `922c980..ca5ab97` (author identity reused from the previous commit).
  - The push started no run: `native-metal-check.yml` is `workflow_dispatch` only. Dispatched it on the branch:
    iOS build run 43441211287 = success (first compile of 0014). Host integration workflow NOT run (light flow).
  - Patch 0014 applied to the local `xiaomi13-layout-backport-20261112\native\` work tree so the reviewed sources
    match. `verify-ci-artifact-0016.py` (0014's verifier + two marker checks) -> `artifact-verification-0016.json`,
    `ios-artifact-0016\`, `lost-reason-ios-artifact.zip`: artifact 14046486057, zip sha256 `241c0583...5b50`, 19
    manifest files, digest matches GitHub, reviewed sources identical, `FUXI NMB VK ERROR: [` in the bridge and the
    no-description text in FuxiNativeMetal. Changed binaries only: `libnative_metal_bridge.dylib` `cff40da5...d46b`
    (was `fd9a30a1...`), `FuxiNativeMetal` `457fcf6b...b2c9` (was `413439f7...`); libmetal2vulkan and the smoke
    tool are unchanged.
  - Host staging: `native-layout0016-share\` (0014's share with `native\` replaced), overlay
    `native-layout0016-overlay.qcow2` over `native-layout0015-root.img` (re-hashed `ddcc8718...1677`), `guest-run.py
    ... stage-test.sh INSPECT_SUCCESS` -> `native-layout0016-stage.log`: GUEST_OK, fsck OK three times, staged
    hashes equal the artifact's. Export `native-layout0016-root.img`, 21474836480 bytes, sha256
    `a93b1da45c795798307130d956c0af2d70164a100d270d013ac60d03c91e55d3` (`.sha256` written); differs from 0015 in 21
    of 5120 blocks. So 0016 = 0015 (About fix kept) + diagnostic bridge/frontend.
  - NOT done: transfer, guarded write, readback, boot (each needs the user's approval). Test plan: boot the
    `native-surfrow2-novmd-loader`, open Settings > Wallpaper, let the script (or the user) swipe home, then read
    `FUXI NMB VK ERROR` and the refusal text around the `gate runtime-failure` line.
- **0016 deployed and booted once** (user: "部署並開機"). The phone's current root is 0016.
  - map -> `verify-native-map.py` OK (inode 199819, 39 extents). `native-layout0016-transfer.log`: 346 differing
    blocks (1384 MiB), `STAGE_VERIFIED ... a93b1da4...55d3`. Phone image before the write: sha256
    `9d802518cfcb933d5cba90b510bd6035060e3b7ab3525e2fdd891242ff9e1dd9` (0015 + four boots, not kept).
    `native-layout0016-write.sh` (`4fec5952...3e0f`) via `native-layout0016-supervisor.sh` (`fa275986...d23a`), both
    generated from the 0015 pair by string replacement: status 0, 22.6 s; unmap/remap -> readback
    `a93b1da4...55d3` = READBACK_MATCH. Stage file removed.
  - Boot 22:44:55, `native-surfrow2-novmd-loader`, 240 s, user at the phone. Evidence
    `native-layout0016-novmd-boot-1\` (boot.log 13955 lines, sha256 `8ff2bdd5...8cba`),
    `native-layout0016-novmd-monitor-1.log`. Afterwards Android, `mapped=false`, 100 %, 40.0 C.
  - **NOT reproduced**: 0 DeviceLost lines, no `gate runtime-failure`, hence no `FUXI NMB VK ERROR` line either (the
    diagnostic stays unexercised). The wallpaper screen was reached: Preferences exec 5613, PosterBoard crash 6981,
    AegirPoster crash pairs 7287-7319 and 7715-7733, PosterBoard crash 7905; the touch script started later at 10479
    after another Preferences exec (9040). `full 0`, `peak_bos 1295` (1823 in the lost boot), 207 touch-downs.
    Crashes: bluetoothd/fairplayd x21, vmd x20, assetsd x9, AegirPoster x4, PosterBoard x2, mediaserverd x1,
    driverkitd x1.
  - So "wallpaper screen + swipe home" is not sufficient either. Score: 2 losses in 6 boots with manual use.
- User: "先修性能問題" (performance first; the DeviceLost hunt waits for the next natural occurrence).
  - Baseline numbers from the 0016 boot (novmd loader, swap wait ON, user in Settings): shown 600-frame windows
    22.1, **71.5** (600 in 8391 ms, 308/600 single-period), 53.2, 37.6, 35.7, 39.6, 39.3, 36.8, 38.1 fps;
    `native swap wait avg_us` 7192-7455 over 6000 swaps (max 138.7 ms); kernel fence waits avg 2.5-9.5 ms per 600;
    boosted 0-3.9 s per 10 s window, governor max 65.5 C. The swap wait alone is about 7.3 ms of every frame, i.e.
    most of a 120 Hz period spent blocked.
  - Experiment 15 loader prepared on the host, NOT booted: `native-surfrow2-novmd-nowait-loader` = spin loader +
    `sweetnoexec=...,vmd`, `fuxigpuncimport=308800` (bit 12, as experiment 11), `fuxiperf=1 fuxicores=0x7c
    fuxiperfbudget=500 fuxiswapwait=0`; `fuxicpuload=10` dropped to fit (606 chars). payload `5250d4ce...37ec`,
    boot.img `d7ce5ce9...3e37`. (A first build with a malformed swap token was deleted and rebuilt.)
- **Experiment 15 RUN** (user: "開機實驗 15"), 22:57:54, 240 s, user at the phone (220 touch-downs; Settings x9,
  Photos). map/verify OK, loader verify OK. Evidence `native-layout0016-novmd-nowait-boot-1\` (boot.log 18292
  lines, sha256 `27312d5a...a0d2`), `native-layout0016-novmd-nowait-monitor-1.log`. Afterwards Android,
  `mapped=false`, 100 %, 39.4 C.
  - No panic, HANG, DeviceLost or gate failure; `reuse_before_show=0` in all 16 stats lines; `full 0`,
    `held 799 hold_expired 349`, `peak_bos 1838`; 10817 submits (6036 in the 0016 boot with the wait on).
    Crashes: bluetoothd/fairplayd x21, vmd x20, assetsd x3, driverkitd x1 (no Poster crash this time).
  - Shown 600-frame windows in use: **86.0** (600 in 6976 ms), **85.1**, 66.4, 41.5, 70.7, 58.7, 59.6, 55.9, 55.7,
    65.8, 53.6, 47.8, 29.9, 58.4 fps. Start gaps of the two best windows: 227/370/3/0 and 196/404/0/0, i.e. every
    frame shown within two TE periods and a third within one. Against the wait-on boot of the same image (best
    71.5, typical 36-53) this is the largest single gain of the day.
  - Kernel fence waits avg 3.9-9.3 ms per 600 (unchanged: the GPU work per frame is the same, it is just no longer
    serial with the next frame's CPU work). Boosted 0-3.2 s per 10 s; governor max 65.5 C, host TSENS max 61.5 C.
  - User, during the boot: "感覺流暢度有提升喔". Flicker verdict: asked, not yet answered.
  - User on flicker: "沒有閃爍". So `native-surfrow2-novmd-nowait-loader` IS the baseline loader; the swap wait
    (patch 0013) is no longer needed with bit 12 on this kernel, by eye and by `reuse_before_show=0`.
  - Where the frame time goes now (backboardd's own lines in this boot): windows up to **94.98 Hz** (475 frames),
    others 51-82 Hz; `gpu_ms` 4.2-10.8 (typ. 6-7); `gpu_busy_est_pct` 31-58 %. So the GPU at 680 MHz is about half
    idle and is no longer the throughput bound; with the wait gone throughput = max(CPU per frame, GPU per frame),
    and the CPU side (about 10.5 ms per frame at the best window, gold mostly at 1.4 GHz) is the bound (my reading).
    Shown frames trail production (86 shown against 95 produced at best) because of the START window.
  - TSENS at 120 s in this boot: gpuss-0..7 57.3-59.7 C, cpu-1-x 54-60 C: the GPU block is as hot as the CPU
    cores, so its clock is part of what trips the 62 C governor guard.
  - Experiment 16 loader prepared on the host, NOT booted: `native-surfrow2-novmd-nowait-lvl6-loader` = baseline
    with `fuxigpulevel=6` (550 MHz). Idea: give up GPU clock that is not being used to buy CPU boost time. Expected
    `gpu_ms` about +24 %; whether the temperature drops enough to matter is unknown.
- Reading: the governor as shipped trades speed for temperature; it cannot be set to "mostly high" by boot args
  (budget max 500 ms/s, 68 C ceiling fixed). A mask without the governor needs a kernel change (`fuxicores` is
  only read in `fuxi_perf_start` after `fuxiperf=1`).
- **Experiment 16 RUN** (user: "開機實驗 16", new session), 23:11:57, 240 s, user at the phone.
  `native-surfrow2-novmd-nowait-lvl6-loader` (boot.img `b69d26a5...727b`, payload `5569e7f6...8ef9`). map ->
  `verify-native-map.py` OK (inode 199819, 39 extents) -> loader verify OK -> unmap. Evidence
  `native-layout0016-lvl6-boot-1\` (boot.log 17985 lines, sha256 `031277a4...deec`),
  `native-layout0016-lvl6-monitor-1.log` (ends with the usual USBError at the probe reset). Afterwards Android,
  `mapped=false`, 100 %, 39.6 C.
  - `FUXI GPU CLOCK: level=6 ... requested_khz=550000 ... hfi_ack=1`. No panic, HANG, DeviceLost or gate failure;
    `reuse_before_show=0` in every stats line; `peak_bos 2464` (highest so far; limit 4096).
  - Shown 600-frame windows: 81.2, 80.1, 80.9, 58.5, 53.3, 50.0, 45.7, 74.1, 39.8, 53.9, 62.2, 51.7, 40.9, 38.3 fps
    (experiment 15: best 86.0/85.1, typical 54-71). No gain.
  - Boost time did NOT increase: summed `boosted` over the boot 17.8 s against 17.6 s in experiment 15; per 10 s
    window 0-3.5 s; unboosted gold average still 1401-1422 MHz. Governor max temp 64.3 C (65.5 C), host TSENS max
    61.5 C (same). So dropping the GPU clock to 550 MHz bought no CPU boost time.
  - Cost: backboardd `late` 809 at 10383 frames against 287 at 10745; `gpu_ms` typ. 7-9 (was 4-8),
    `gpu_busy_est_pct` up to 59.5 % at 68-80 Hz windows; kernel wait avg 1.2-4.8 ms.
  - User: "本次 我沒有感覺不出太多差別". Verdict: level 6 is not adopted; `native-surfrow2-novmd-nowait-loader`
    (level 8) stays the baseline. GPU clock is not the lever for the 62 C guard (my reading: the CPU cores heat
    themselves; the workloads of the two boots were not identical, so this is a comparison of ranges, not an A/B).
  - Remaining lever on the CPU side is the governor itself (unboosted gold pinned at 1401.6 MHz, boost refused
    above about 62 C): needs a kernel change, not boot args.
- **Governor diagnostic patch prepared** (user: "好，備改動"), harness work tree `kernel-bo-slots-work`, branch
  `codex/kernel-bo-slots`, **uncommitted, not pushed, not compiled** (no C compiler on this host; upstream
  `tools/ci/build.sh:249` runs `test-fuxi-perf.py` with clang -Werror and `sync-fuxi-perf.py --check` in CI).
  - `patches\kernel-perf-governor.patch` (sha256 `c6d14fc5...1d5c`; `xnu/fuxi-perf/fuxi_perf.c`,
    `fuxi_perf_logic.h`, and 0097 regenerated with `tools/sync-fuxi-perf.py`), `tests\test-perf-governor.py`,
    workflow edited (two path lines, apply + test). Sources edited in place in
    `E:\Codex-Xiaomi13-GPU-20261109\kernel-source-979` (CRLF work tree; that tree has only the stride patch of the
    harness stack applied, so `test-bo-slots.py` / `test-surfrow-mode2.py` fail there; not related).
    `git apply --cached --check` against the pristine index passes; no other harness patch touches 0097.
  - All three are opt-in; with none given the policy is the legacy one (caps, 62/68 C, duty 200 unchanged):
    - `fuxiperfbase=MHz` (1402..2400): gold/prime vote while NOT boosted and NOT thermally blocked (new third
      level `fp_base`); a blocked guard still drops to the legacy cool level (1401.6 MHz). Log line
      `FUXI PERF: ready base LUT gold .. prime .. kHz`.
    - `fuxiperfmax=dC` (620..800): sets limit=N, hot=N-60, resume=N-100, start=N-80 (the default spacing), then
      the individual args may still override. `fuxiperflimit` upper bound 680 -> 800; the hard-coded 680 run
      ceiling in `fp_want_boost` is now `fp_run_max` (= max(limit, 680)).
    - `fuxiperfbudget` upper bound 500 -> 1000; 1000 disables the duty cut (thermal guard only).
    - Any of them prints `FUXI PERF: diagnostic policy base .. max .. run max .. duty ..`.
  - Boot-args room: the field is at 606/607. Planned experiment 17 args: drop `fuxitouchscript=150
    fuxitouchscriptn=2` (39 chars; the user is at the phone anyway) and add e.g. `fuxiperfbase=1786
    fuxiperfmax=720`, budget `500` -> `1000`. Values are a proposal, not decided. 72 C hot-trip = 78 C ceiling is
    below the 78.9 C the phone already reached with no governor; boots stay bounded at 240 s.
  - Next: commit + push (user approval) -> push-triggered CI (about 10 min) -> artifact -> loader with
    `prepare-kernel-variant-loader.py` + `prepare-args-on-loader.py` -> boot (user approval).
- User approved ("commit 並 push"): harness commit `9b9ca88` on `codex/kernel-bo-slots`, pushed `bd72669..9b9ca88`
  (author identity reused from the previous commit via `-c user.name/-c user.email`; no global identity is set).
  - CI run 43452273953 = success, all steps. build.log has the upstream `PASS: predictive/rise/62C cutoff ...` and
    `PASS: 0097 governor snapshots match canonical sources`, i.e. the first compile of the change and the upstream
    governor model test (clang -Werror, ASan/UBSan) both passed with the legacy defaults.
  - Artifact `E:\Codex-Xiaomi13-GPU-20261109\kernel-perfgov-artifact\`: kernel `3b796ada...780b` (matches
    `SHA256SUMS`), unstripped `b39b6194...16e2`; strings `fuxiperfbase`, `fuxiperfmax`, `diagnostic policy base`,
    `ready base LUT` present once each. The kernel is 120 bytes longer than surfrow2 (payload +112).
  - `prepare-kernel-variant-loader.py` refused at first: its "loader body byte-identical" check failed because the
    payload length changed. The 12 differing bytes are the Image size field and five `ADD Xd, Xn, #imm12`
    immediates, each +112. The check now accepts exactly that (size field + ADD immediates whose delta equals the
    payload length delta) and prints it; anything else still fails.
  - `native-perfgov-spin-loader` (kernel-only variant of `native-surfrow2-spin-loader`): payload `c1e01b7e...4cf9`,
    boot.img `5774ea2e...4180`. NOT booted.
  - **Experiment 17 loader** `native-perfgov-base1786-loader` (from the above, host only, NOT booted): baseline
    args with `fuxitouchscript=150` -> `fuxiperfbase=1786`, `fuxitouchscriptn=2` -> `fuxiperfmax=720` (token
    replacement, so the touch script is OFF), `fuxiperfbudget=1000`; 602 chars. payload `86833490...f31e`, boot.img
    `7f267669...e919`. Expected: gold 1785.6 / prime 1708.8 MHz while not blocked, limit 72 C, hot 66, start 64,
    resume 62, no duty cut. Verify with `verify-args-on-loader-payload.py`; monitor needs
    `run-monitor.ps1 ... -Kernel kernel-perfgov-artifact` (new parameter, default unchanged).
  - Check in the log: `FUXI PERF: diagnostic policy base 1786 MHz max 720 dC run max 720 dC duty 1000/1000 ms`,
    `ready base LUT gold 1785600 prime 1708800 kHz`, `freq unboosted gold` average near 1786 when cool.
- **Experiment 17 RUN** (user: "開機實驗 17"), 23:40:30, 240 s, user at the phone, touch script OFF. map/verify OK
  (kernel-variant verify of the spin loader + args verify of the experiment loader). Evidence
  `native-layout0016-perfgov-boot-1\` (boot.log 15732 lines, sha256 `c5b1b8d8...88ac`),
  `native-layout0016-perfgov-monitor-1.log`. Afterwards Android, `mapped=false`, 100 %, 37.6 C.
  - New kernel boots; policy lines as expected: `thermal hot 660 limit 720 resume 620 start 640 ... duty
    1000/1000`, `diagnostic policy base 1786 MHz max 720 dC run max 720 dC`, `ready base LUT gold 1785600 prime
    1708800 kHz`. No panic, DeviceLost or gate failure (a case-insensitive HANG grep only hits "change");
    `reuse_before_show=0`; `peak_bos 1643`.
  - Boost time rose a lot (max/budget work): per 10 s window 2.6-8.5 s boosted in use (0-3.2 s in experiment
    15), boosted gold avg 1.5-2.3 GHz. Governor max temp 70.2 C (65.5 C before), host TSENS max 60.7 C.
    Cuts are thermal now (cuts about equal boosts; duty is off).
  - **The base level did NOT do what was intended.** In the quiet, never-blocked windows the measured gold
    frequency (EPSS +0x20) averaged 499-615 MHz with max 1785; with the legacy cool vote it sat at a constant
    1401 (experiment 15, same windows). So with the 1785.6 vote the domain mostly reports its LOWEST row and
    only touches 1785 briefly. In use the unboosted average is about 1417 (mostly the blocked -> cool state after
    a thermal cut). Cause NOT established. Also seen: prime reports 1708 for the 1363.2 cool vote and up to 2092
    for the 1708.8 base vote, so for prime the voted row and the reported frequency do not line up (was already
    so before this change). Do not treat `fuxiperfbase` as working.
  - Shown 600-frame windows: 72.7, 59.9, 71.0, 75.4, 47.1, 67.2, 70.1, 67.8, 55.6, 63.2, 45.6, 62.6 fps
    (experiment 15: best 86.0/85.1, typical 54-71). backboardd windows 63-89 Hz in use (8 of 12 above 63), `late`
    220 at 8678 frames. Not an A/B (no touch script here, different apps); the typical window is a little higher,
    the best is lower. `gpu_busy_est_pct` up to 58 % at 88 Hz.
- User after experiment 17: "我不確定 你自己再自行優化到目標 中途不用問我 必要時再提問 可以參考別的專案有沒有思路".
  Standing go-ahead for temporary boots, harness pushes and loader rebuilds in this smoothness task (memory
  `xiaomi13-autonomous-optimization`). From here on boots use the built-in touch script as the load.

## 2026-11-13/14 night: where the frame time goes (experiment 17 log) and four scripted boots

- Reading of the best experiment-17 window (boot.log 4044-5030, backboardd 94 Hz, shown 72.7 fps): client GPU
  `gpu_ms` 6.8 (fence waits avg 6.5 ms), scanout blit 0.6-1.0 ms (exclusive on the GPU), submit `retry_sleep`
  0.9 ms per submit (refusals busy/hold 133/184 per 600), so about 8.7 ms of serialized GPU-side time per frame
  against an 8.26 ms TE period; GPU busy about 73 % at 94 Hz. Flip gaps 164/353/68/15 (27 % single period).
  `ready_present_us` averages one full TE period (latency, not throughput). Gold averaged 2.2-2.3 GHz in that
  window and the rate is the same 94-95 Hz that experiment 15 reached at 1.4 GHz: **CPU frequency is not the
  bound; the per-frame GPU time is** (my reading; no per-pass GPU profile exists on the native path).
- The EPSS +0x20 "current frequency" drops to the lowest row when cores idle at high votes (boosted windows with
  low load also average 1.3-1.5 GHz), so the experiment-17 "base did not hold" reading is probably idle
  reporting, not a failed vote. Not verified; `fuxiperfbase` is unneeded either way.
- Tools: `boot-exp.ps1 -Loader .. -Out ..` (map, verify, unmap, fastboot boot, monitor via WMI, wait for
  Android; waits for battery <= 40.0 C first), `script-metric.py <out dir>` (aggregates over the touch-script
  span). `verify-args-on-loader-payload.py` now also accepts an out-of-scope token when it is the new side of a
  listed swap. Loaders derive from `native-perfgov-spin-loader` with `fuxitouchscript=25` and the
  `fuxitouchscriptn=2` slot replaced by the variable under test (default 10 loops, 23.5 s each).
- Four boots on 0016 + perfgov kernel, nobody at the phone (evidence `native-layout0016-s25-*-boot-1\`):

  | loader (`native-perfgov-s25-*`) | variable | backboardd Hz in script | gpu_ms mean | single-period flips | boosted | max temp |
  | --- | --- | --- | --- | --- | --- | --- |
  | ref (n=9) | none (budget 500) | 54.6 | 7.53 | 34 % | 34.6 s | 65.9 C |
  | max720 | `fuxiperfmax=720`, budget 1000 | 36.7 | 8.46 | 36 % | 79.9 s | 64.7 C |
  | ddr8 | `fuxigpuddr=8` (14398437 kB/s acked) | 36.8 | 7.77 | 38 % | 46.2 s | 64.7 C |
  | gmem | `fuxigpugmem=1` | 44.1 (to the loss) | 7.60 | 33 % | - | 65.1 C |

  - The script's average frame rate is demand-limited and differs between boots (ref 39.8 fps shown, the others
    33.2), so only the per-frame numbers are compared. Neither more CPU boost nor a higher DDR vote changed
    `gpu_ms` or the single-period share beyond boot-to-boot spread (no repeat of the reference was run, so the
    spread itself is not measured).
  - **GMEM: not usable.** `fuxigpugmem=1` does reach the native path (IBs per submit rose from about 24 to
    90-120), `gpu_ms` did not improve, and backboardd lost the device in script loop 4 (boot.log 8282,
    `gate runtime-failure`, CPU respawn). The 0016 diagnostic printed an EMPTY `last:` and no `FUXI NMB VK ERROR`
    line, so Mesa does not route the loss reason through the messenger: the next diagnostic must be in the
    runtime or Mesa. The gate latch is per boot (`gate new-boot reset`).
  - Lead checked and NOT supported: "a submit over 128 IBs causes the loss". Max before the loss was 122 here and
    127 in the 0015 novmd boot, and the last submits before that loss had 5-7 IBs.
- Upstream notes read (`docs-lanes\gpu-fps.md`, `gpu-clock.md`, `gpu-translation.md` GMEM trial,
  `unixnu-port.md` AdrenoMetal table): sysmem + no concurrent binning is deliberate (UniXNU hangs with GMEM /
  concurrent binning under per-switch context reset); upstream's own guess is that sysmem makes the
  bandwidth-heavy blur passes slow; their GMEM trial changed nothing. Level 8 already votes DT DDR index 7.
- Open levers, none tried: (1) a smaller CA canvas (it renders 1125x2436, 2.74 MP, and the scanout blit copies it
  to the panel) - needs the scanout blit to scale and a consistent device class in the ADT; (2) iOS "Reduce
  Transparency" (removes the blur passes; user-visible); (3) removing the scanout copy (direct scanout);
  (4) the 0.9 ms submit retry sleep.

## 2026-11-14 00:37 js boot (job stamps) and the per-IB GPU profile kernel (written up in the next session)

- User 2026-11-14 (after the four scripted boots): "不使用1 2 方式 你繼续 可以挖底層驅動嗎" (no Reduce Transparency, no smaller
  canvas; dig into the driver stack, any layer). Then: "中途非必要不要中斷工作 直到完成性能優化目標 至少跑滿屏幕刷新率" - keep
  iterating without stopping until at least the panel rate (120 Hz). Memory `xiaomi13-no-visual-tradeoffs`, `xiaomi13-autonomous-optimization`.
- Boot `native-layout0016-s25-js-boot-1` (loader `native-perfgov-s25-js-loader` = s25 reference with `fuxiearly=8` in the
  `fuxitouchscriptn` slot, perfgov kernel, nobody at the phone): kernel job stamps (CP always-on counter around each job, BR thread).
  Per 600 jobs: submit_to_start avg 29-49 us, **exec avg 7.5-9.4 ms (max 24 ms)**, end_to_observed avg 74-94 us, no_stamp 0.
  Script metrics: backboardd 37.0 Hz, gpu_ms mean 8.38, 38 % single-period flips. About one job per backboardd frame (6973 submits).
  So the ~8.4 ms per frame is GPU execution itself; queueing and retirement add < 0.15 ms. IBs per submit 3-69 (peaks at 3, 9, 13,
  17, 23, 27, 37, 47: a lead plus encoder/barrier pairs, metal2vulkan `command_buffer/submit.rs`).
- Next instrument: **per-IB profile** (harness `codex/kernel-bo-slots` commit 1b75b23, `patches/kernel-ib-profile.patch`,
  `tests/test-ib-profile.py`, CI run 43469873478). Opt-in `fuxiearly` bit 16 (value 16; bits 1/4/8 are taken). One job in 96, one
  at a time, gets `CP_WAIT_FOR_IDLE` + `CP_REG_TO_MEM` of the always-on counter before each IB and after the last (fence page
  1536 + 16*i; 0-216 and 1024-1160 are used). The stamps do not touch CP_THREAD_CONTROL: Turnip's `tu_init_hw` starts every
  primary with BOTH, but one command buffer can span several IBs and end one on BR inside a render pass (Mesa 85fda3b
  `tu_cmd_buffer.cc`). At submit the kernel reads each IB1 and its IB2s through the BO's kernel mapping (fault-contained,
  24576-dword budget, 4096 per IB) and records draws, CP_BLITs with GRAS_A2D_DEST area, dispatches, IB2 calls, WFIs, waits,
  SET_DRAW_STATEs, window-scissor count/area (render passes), RB_MRT[0] BUF_INFO/PITCH, RB_A2D_DEST_BUFFER_INFO,
  TPL1_A2D_SRC_TEXTURE_INFO/SIZE (A7XX offsets from Mesa a6xx.xml). Retirement prints `FUXI IBPROF: job ...` (time per kind:
  empty / full_rt (>= 2 MP) / part_rt / blit / compute / unread) and the 6 longest IBs; every 16 samples a per-kind average.
  Host test (MSVC, stubs, the real `fg_abi_frame`): insertion order, slot zeroing, decode of a synthetic IB1/IB2, budgets, the
  32-bit counter wrap, sampling and stale-sample recovery all pass (scratchpad only, not kept).
- Side boot while CI queued: `native-layout0016-s25-fxb-boot-1` (loader `native-perfgov-s25-fxb-loader` = s25 reference with
  `fuximetal=native=>fuximetal=fuxi`, upstream's own FuxiMetal Vulkan backend). backboardd reached only 1-13 Hz (gpu_ms 6.4-6.8 at
  that rate), then `gate runtime-failure next-respawn=CPU` (boot.log 4072) and CPU rendering for the rest; shown 16.1 fps.
  Not a usable path; the native backend stays. (Upstream's own numbers for that backend on the same CA load: gpu_exec ~8.6-10 ms,
  ~115 draws and 71-73 render passes per frame, `docs-lanes/gpu-translation.md:1084-1100`, so it would not be faster on the GPU.)
  Tool note: do not append `2>&1` to `boot-exp.ps1`; with `$ErrorActionPreference='Stop'` fastboot's stderr aborts the script
  right after `fastboot boot` (the monitor then has to be started by hand, as was done here).
- Kernel patch updated before the CI ran (harness commit "Count event writes and CP waits", CI 43471408711): the per-IB lines also
  carry `wfm` (CP_WAIT_FOR_ME), `ev all/ccu/cache` (CP_EVENT_WRITE7 count, CCU flush/clean/invalidate events, cache
  clean/flush/invalidate events), and CP_WAIT_TIMESTAMP counts as a wait. Analyzer: `E:\...\ibprof-summary.py <out> [--script]`.

## 2026-11-14 01:30 lead: every draw runs with FLUSH_PER_OVERLAP (rasterization order on every pipeline); metal2vulkan patch 0003

Source chain (Mesa 85fda3b fetched from gitlab.freedesktop.org into the scratchpad; metal2vulkan 720ef1c = local
`m2v-native-research`):
- metal2vulkan `render_pass.rs:1482`: `fetch_inputs = !colors.is_empty() && framebuffer_fetch::through_inputs(profile)`, and
  `through_inputs` is `rasterization_order_color_access` (Turnip has VK_EXT_rasterization_order_attachment_access). So on the
  phone EVERY render pass with a color attachment is a fetch pass: colors are also input attachments, GENERAL layout.
- `pipeline/graphics.rs:819`: `blend_flags = RASTERIZATION_ORDER_ATTACHMENT_ACCESS` whenever `key.pass.fetch_inputs`, i.e. on
  every graphics pipeline, fetching or not (comment: "so a fetch reads what an earlier fragment of any pipeline wrote").
  The pipeline's own `graphics.rasterization_order_attachment_access` is true only when its fragment reads input attachments
  (`metal2vulkan/src/pipeline/link/graphics.rs:544`).
- Turnip `tu_pipeline.cc:4396-4430`: a pipeline with that blend flag gets GRAS_SC_CNTL `single_prim_mode = FLUSH_PER_OVERLAP`
  (`prim_order.state_gmem`); `tu_cmd_buffer.cc:1199-1209`: on chips with `has_coherent_ubwc_flag_caches` (all A7XX,
  `freedreno_devices.py:788`, FD740 is a7xx_base+gen2) that state is applied in GMEM, SYSMEM and binning alike. The sysmem-only
  prim-mode state (feedback loops, `FLUSH_PER_OVERLAP_AND_OVERWRITE`) is not emitted on A7XX. The subpass flag
  `RASTERIZATION_ORDER_ATTACHMENT_COLOR_ACCESS` is stored (`tu_pass.cc:1188`) but not used.
- So every CoreAnimation draw ran in single-primitive overlap-flush mode: a primitive overlapping an earlier one's pixels waits
  for them. CA layers overlap almost everywhere. Upstream found the same in its own backend and added `FUXI_MTL_FETCH_SPLIT`
  (`tools/fuxi-metal/TranslationPolicy.hpp:70`: only fetch pipelines carry the flag, plus a by-region color-write ->
  input-read barrier before a fetch draw that follows a non-fetch draw). Not measured by upstream in the docs.
- Patch `metal2vulkan-fuxi-0003-fetch-order.patch` (harness `codex/astc-layout-backport`, iOS CI 43473592150): on iOS the
  blend flag follows `graphics.rasterization_order_attachment_access` (only fetching pipelines); `FUXI_M2V_ORDER_ALL=1` restores
  the old behaviour (default elsewhere). No barrier is added: the flush is the reading draw's, so a fetching primitive still
  waits for overlapping fragments of earlier draws whether or not they were flagged (my reading of the hardware mode, same
  mechanism the blob uses for coherent advanced blend; NOT verified on device - check fetch-using UI (vibrancy, dock, folders,
  Control Center) for wrong blending after deploy). Verifier `verify-ci-artifact-0017.py` (0016's + the
  `FUXI_M2V_ORDER_ALL` marker in `libnative_metal_bridge.dylib`, where the runtime is linked).

## 2026-11-14 01:43 first per-IB profile (baseline 0016) and the copies it found; patches 0003 (built) and 0004 (building)

- Kernel CI 43471408711 success -> `kernel-ibprof-artifact` (kernel sha256 `77502133...19f9`, 16 KiB+ bigger than perfgov).
  `prepare-kernel-variant-loader.py` refused: payload delta 17008 bytes moves five ADRP+ADD address pairs, and its guard only
  accepted ADD immediates (< 4096). The guard now accepts exactly ADRP/ADD pairs on one register whose computed address moves
  by the payload delta (backup of the old tool in the session scratchpad). Loaders `native-ibprof-spin-loader` (kernel
  variant of perfgov-spin, verified) and `native-ibprof-s25-loader` (s25 reference args with `fuxiearly=16`).
- Boot `native-layout0016-s25-ibp-boot-1` (01:43, script, nobody at the phone; boot.log 13485 lines, sha256 `14dba67a...635a`):
  backboardd 38.0 Hz gpu_ms 8.29 (same as js boot: the per-IB WFIs on 1 job in 96 cost nothing visible), 40 % single-period
  flips, no faults, 74 sampled jobs, 0 unusable. `ibprof-summary.py ... --script`:
  - span mean 8573 us (median 7943), 26 IBs/job. Time by kind: **full_rt 75.7 %** (6.5 ms/job: IBs whose last render pass is
    the whole 1125x2436 target), part_rt 18.3 %, empty (barrier IBs) 1.6 %, blit-only 0.8 %, unread 3.6 %.
  - The full-target IBs carry ~30-100 draws AND ~8 Mpx of CP_BLIT each (3 full-screen copies): last blit
    `TPL1_A2D_SRC_TEXTURE_INFO 0x501330` (BGRA8, TILE6_3) size 1125x2436 -> `RB_A2D_DEST_BUFFER_INFO 0x430` (BGRA8,
    linear, no UBWC). Small-target IBs carry the reverse 1125x2436 copy linear -> tiled (`s2 500430 -> d2 1330`, 2.7 Mpx).
    One IB with ONE draw and 3 blits took 1070 us. ~3-4 such IBs per frame => roughly 3-4 ms/frame of pure copying.
  - RB_MRT[0] BUF_INFO/PITCH read 0: the color target state is not in IB1/IB2 (draw-state groups, not followed).
- Cause (source, metal2vulkan 720ef1c): an IOSurface texture is a texture over a buffer (`buffer_texture.rs`); it aliases a
  linear image only if `linear_plan::holding` allows the bytes between rows. The display surface's rows are 4608 bytes for
  4500 bytes of texels (kernel sweetsurfrow rounds to 256; Turnip's linear pitch for BGRA8 is also 256-aligned, so pitch and
  features (Turnip `linear = optimal`) match), and `holding` lets an image hold padded rows only for a texture that only reads
  (`linear_plan.rs:805-840`: Vulkan load/store ops and layout transitions may write the padding, Metal never does). So the
  display surface (a render target) is a shadow: an optimal image copied in before and back after every encoder that draws it.
- Patch `metal2vulkan-fuxi-0004-surface-alias.patch` (harness 786d273, iOS CI 43477303711): in `Device::storage_over_buffer`
  (`texture.rs`), for `new_texture_with_iosurface_plane` on iOS a padded-row refusal becomes `Holds::Padding` (alias);
  `FUXI_M2V_SURFACE_SHADOW=1` keeps the shadow. Rationale: the padding is the surface's own alignment, read by no client
  (the scanout copy crops 1080 of 1125 columns). Risk to check on device: any surface whose padding someone reads.
- Image 0017 (= 0016 + patch 0003 only): verified artifact 14061929644 (`fetch-order-ios-artifact.zip` e55c6496...b340, only
  `libnative_metal_bridge.dylib` changed, `697a0b64...`), staged via `guest-run.py` (GUEST_OK, fsck OK), exported
  `native-layout0017-root.img` sha256 `f9979cc84d866a73dce93890e110d5bf19e1beae124418c9892a83294c227390`, 22 blocks differ from
  0016. E: is now 2.4 GB free: the next image goes to D: (100 GB free).
- **0017 deployed** (light flow): transfer 433 blocks (1732 MiB) `STAGE_VERIFIED f9979cc8...7390`; phone image before
  `61b39e51...2b86`; writer `native-layout0017-write.sh` (`eb23e4ac...26e8`), supervisor (`3cc66f52...bc71`) derived from 0016's;
  status 0, 25.3 s; READBACK_MATCH; stage removed. Note: `verify-native-map.py | Select-Object -First N` fails the exit code
  (broken pipe), capture its output instead.
- Boot `native-layout0017-s25-ibp-boot-1` (patch 0003 alone, same loader): backboardd 34.0 Hz gpu_ms 8.62, 40 % single;
  profile 65 jobs: span mean 7868 us (baseline 8573), median 7765 (7943); full_rt 82 %; the full-target IB still ~1.8 ms with
  ~28 draws and ~9.7 Mpx of blits. So FLUSH_PER_OVERLAP on every pipeline was at most a few percent; the copies dominate.
- Image 0018 = 0017 + patch 0004 (artifact 14064800119, zip `627034e6...cd43`, only the bridge changed): staged on
  `D:\fuxi-images\native-layout0018-overlay.qcow2` (backed by the 0017 raw on E:), exported
  `D:\fuxi-images\native-layout0018-root.img` sha256 `4c10332db264d2c3b0939fdf8f32641f765298e7d63ab275a7fd2c9d86128b34`
  (21 blocks differ from 0017). Deployed: 194 blocks (776 MiB), phone before `9e3e3acf...1885`, writer `5598f22c...53c6`,
  supervisor `9d8f506d...de11`, status 0, 23.9 s, READBACK_MATCH. **The phone's current root is 0018.**
- Boot `native-layout0018-s25-ibp-boot-1` (patches 0003+0004, same loader, script): **backboardd gpu_ms mean 6.64**
  (baseline 8.29), late +36 (baseline +575), 34.8 Hz (demand-limited), 39 % single-period flips (unchanged: the script is
  demand-limited and the GPU only ~25-30 % busy). Profile 67 jobs: span mean 6755 us (8573), median 6476; part_rt
  1235 -> 527 us/job; small-target IBs lost their 2.7 Mpx copies (284x611: 1769K -> 169K px of blits). Remaining blits in
  full-target IBs: destination `0x1330` = BGRA8 TILE6_3 **UBWC** (internal render textures), many with no source
  (`s2 0/0`: 2D solid fills = clears, 2.68 Mpx each) plus small linear->tiled copy-ins (A8 masks `0x500002`/`0x50000f`,
  ~180x180 BGRA `0x500030`).
- Workload for sustained demand: the touch script swipes 250 ms then sleeps 1.2-2 s (CA idle most of the time). `fuxitaps`
  sleeps a fixed 1.2 s after each swipe but the swipe's own duration is free, so a slow drag renders continuously. Loader
  `native-ibprof-drag-loader` (from ibprof-spin): `keepsyms=1` -> `fuxitapsloop=9`, `sweetnull=...` -> `fuxitaps=25`
  (sweetnull's default is `backboardd,SpringBoard`, so only peakpowermanagerd/fuxi-ca-smoke output is lost),
  `fuxitouchscript`/`fuxitouchscriptn` -> `fuxiearly=16`/`fuxiswapwait=0`, `+fuxitapcmd=s540,2390,540,999,99;s900,999,180,999,8000;
  s180,999,900,999,8000` (unlock/home, then 8 s drags left and right; 607 chars, the cap). `prepare-args-on-loader.py` now allows
  ';' in a `fuxitapcmd=` value; `script-metric.py` / `ibprof-summary.py` delimit fuxitaps runs (first `loop 1/` to `done`).
- Boot `native-layout0018-drag-ibp-boot-1` (0018, `native-ibprof-drag-loader`, nobody at the phone; boot.log 18721 lines,
  sha256 `2e7ab05b...c432`; the monitor stopped after loop 8): sustained demand works. Per 600-frame windows: **shown 63-81 fps**
  (600 frames in 7.4-9.5 s, 0-8 not shown per window, all `replaced`), backboardd 64-80 Hz (gpu_ms 6.1-7.7, late +0,
  `gpu_busy_est_pct` ~48-55 %), flip gaps 2372/8852/125/50 (21 % single, mostly TWO periods). Profile 134 jobs: span mean 7165
  (median 7380, max 8410); per frame one composite IB of ~5.2 ms (47 draws, 27 blits = 10.4 Mpx, 31 WFIs, 178 events), two
  full-target IBs of ~0.8 ms, small passes. Scanout copy 591-598 us; the client hardly waits for it any more (retry_sleep
  0-25 us per submit, hold waits ~0 per submit); `SWEET FB: stages` ready_present 6.6-7.4 ms avg, submit_fence ~0.78 ms,
  fence_start 1.2-1.6 ms.
  - CPU: unboosted all run (gold 1401 MHz, prime 1708; boosts blocked above 62 C, "above 620 dC" 3-6 s per 10 s).
  - **Userland fence waits average 6.8-7.3 ms, about one per frame** (`FUXI ABI: fence waits per 600 ... avg_us`; pending
    histogram mostly 4-8 ms). With the GPU only ~50 % busy at ~68 Hz, CPU encode (~7 ms) and GPU (~7 ms) look serialized:
    either a completion thread (harmless) or the render thread waiting for each frame's GPU work. Next: `sweetprof=40`
    (backboardd user-stack profiler, `docs-lanes/profiler.md`, 2 ms sweeps for 60 s) on the same drag load.

## 2026-11-14 02:40 sweetprof on the drag load: the display thread is CPU-bound, a fifth of it in `supportsFamily:` audits

- Loader `native-ibprof-dragprof-loader` = drag loader with `fuxiearly=16` -> `sweetprof=40`. Boot
  `native-layout0018-dragprof-boot-1` (boot.log 17763 lines, sha256 `46467024...78eb`): same result as the drag boot
  (68 Hz, gpu_ms 7.21, 20 % single). PROF covered 30000 sweeps (2 ms, 60 s), 35 threads.
- Tools: `prof-sym.py <dyld-cache> <boot.log> [--min N]` (dsc_sym.py adapted to PROF lines; output
  `native-layout0018-dragprof-boot-1-prof-sym*.txt`), `macho-fit.py <addr list> <Mach-O...>`: fits each cluster of non-cache
  return addresses to a host Mach-O by trying 16 KiB-aligned slides and counting addresses that follow a BL/BLR, then
  symbolizes (FuxiNativeMetal slide 0x102c3c000, libnative_metal_bridge 0x1045fc000, libvulkan_freedreno 0x105db8000 in this
  boot).
- `com.apple.coreanimation.render-server` is idle in its mach_msg loop (96 %); the frames are rendered on
  `com.apple.coreanimation.display.primary` (t529), which is ON-CPU ~80 % of the 60 s. metal2vulkan-queue-wait-0 holds the
  ~7 ms GPU fence waits (asynchronous, harmless); completion/handlers/scheduler threads are idle. So the drag rate (~68 Hz
  = ~14.7 ms/frame) is the display thread's CPU time at gold 1401 MHz, not a GPU wait.
- Hot path (22 % of all display-thread samples directly, plus ~8 % in large free/madvise/vm_deallocate rows):
  `QuartzCore CA::OGL::MetalContext::is_viable_texture_from_iosurface` -> `Metal MTLGetTextureLevelInfoForDeviceWithOptions`
  -> `MTLPixelFormatGetInfoForDevice` -> `FuxiNativeMetal flagArgAnswer/askAnswer` -> `nmb_device_answer` ("supportsFamily:")
  -> `metal2vulkan_runtime::families::Device::supports_family` -> `families::audit` (every feature/limit/format row judged
  again, format answers queried per format, a fresh Vec each call). The bridge also ORs in its five target families only
  after running the audit, and `supportsSIMDGroup` audits Apple9 each time.
- Patch `metal2vulkan-fuxi-0005-family-cache.patch` (harness 4abe9d0, iOS CI 43486446044): on iOS `family_rows` and
  `supports_family` are memoized per device (`DeviceShared` address) and family; `FUXI_M2V_FAMILY_CACHE=0` audits every call.
- Kernel round 3 (harness `codex/kernel-bo-slots`, CI 43487040603): `kernel-ib-profile.patch` adds per-IB blit classes
  (`FUXI IBPROF: job N ib K blits clr ..K cp ll/lt/tl/tt ..K big ..K s2 .. d2 .. bc ..`, solid fills by
  GRAS_A2D_BLT_CNTL bit 7); new `kernel-perf-wide.patch`: `fuxiperf=2` = governor with `fuxiperfmax=720` and
  `fuxiperfbudget=1000` as defaults (the boot-args field is full), 0097 resynced with `tools/sync-fuxi-perf.py` (note: on
  Windows it writes CRLF; convert back to LF). Host test of the profile still passes.
- Image 0019 = 0018 + patch 0005 (artifact 14069660078, zip `2d51128f...def4`, only the bridge changed), staged on
  `D:\fuxi-images\native-layout0019-overlay.qcow2` (backed by the 0018 raw), exported `D:\fuxi-images\native-layout0019-root.img`
  sha256 `1c753bec4dedb1ae860263b03ceea475a9ba2d29888fa8a62b3b193acbe23e8a`. Deployed: 235 blocks (940 MiB), phone before
  `a1016ad4...2226`, writer `79f3eea7...a7db`, supervisor `d7769676...c8e3`, status 0, READBACK_MATCH. **Phone root: 0019.**
- Boot `native-layout0019-dragprof-boot-1` (0019 = +0005, same dragprof loader): **backboardd 83.4 Hz** (68.1 before 0005),
  shown 63.2 fps over the whole run (52.0), **single-period flips 56 %** (20 %), windows 73-95 Hz, GPU busy 60-68 %, gpu_ms
  7.0-7.9. The families path is gone from the display thread's top rows.
- Kernel round 3 artifact `kernel-ibprof3-artifact` (CI 43487040603, kernel `0a2e55b9...d956`); loaders
  `native-ibprof3-spin-loader` (kernel variant of perfgov-spin) and `native-ibprof3-dragreg-loader` (drag load,
  `fuxiearly=16`, `fuxiperf=2` without `fuxiperfbudget`, `fuxiregtrace=1`). Boot `native-layout0019-dragreg-boot-1`
  (boot.log 23269 lines, sha256 `9af08075...eeb4`): backboardd 86.2 Hz, 59 % single, boosted 21.0 s (1.8 s with fuxiperf=1;
  governor `hot 660 limit 720`, max 67.8 C). `fk_swipe` already calls `fuxi_perf_input()`; boosts are thermally limited.
  - REGTRACE: per 10 s ~850 `AppleCLCD underrun` (missing key) lookups, ~13 us each in the MIG handler, plus a few
    `Brightness_Scale`; `work_interval_ctl` ~850 calls, 0.2-0.6 ms per 10 s. Kernel-side registry/work-interval cost is tiny.
  - Correction of the sweetprof reading: for a thread ON a core the profiler reads the user registers saved at its LAST
    exception, so "run" rows show the last syscall (e.g. 12 % "in" IORegistryEntrySearchCFProperty = CPU work done after it),
    not where the CPU is. The families finding still holds (its rows were inside malloc/vm syscalls of the audit, and 0005
    removed them with the measured gain).
  - Blit classes (478 full-target IBs): each carries one full-screen solid fill (2.68 Mpx, `GRAS_A2D_BLT_CNTL 0xf03080`,
    destination `0x1330` UBWC tiled) and ~2.6 Mpx of linear->tiled copies (many small linear sources: the runtime's
    copy-in of buffer textures it cannot alias, before every encoder that uses them).
- Kernel round 4 (CI 43514221956, `kernel-prof-live.patch`): sweetprof cross-calls (`cpu_xcall`) the core of each on-core
  thread; the handler copies the live user pc/lr/fp from `find_user_regs()` (fresh when the IPI came from EL0) and reads no
  user memory; one outstanding request per core (a core that has not answered is skipped, so cpu_xcall never waits on one
  of ours), 200 us answer wait. Live samples carry x16=0x7fffffff; `PROF live asked/thread/other/skipped` at the end.
  The host model (`test-fuxi-prof.py`) defines SP_NO_LIVE; the live code was syntax/type-checked with the NDK clang
  against the mock plus stubs (-Wall -Wextra -Werror). 0111's sample.c section regenerated by hand (sync-fuxi-prof.py's
  git-apply via text-mode pipes breaks on Windows); CI runs the upstream test, which re-checks it.

## 2026-11-14 ~06:45 USER VISUAL CHECK of 0019: smoother, but materials/vibrancy broken -> patch 0003 dropped

- Before it: live-PC profiler boots (`kernel-proflive-artifact`, then `kernel-profpc-artifact` with PROFPC 64-byte buckets,
  `prof-pc.py`): display thread on-CPU breakdown (top buckets): memmove 16 %, malloc family ~11 %, os_unfair_lock 8 %,
  pthread mutex 5 %, QuartzCore itself ~9 %, metal2vulkan render path (keep_draw 5 %, draw_resident, Bound::dynamic,
  retain_draw, check_pipeline...), kernel mach traps ~11 %; ~6 % of the thread blocked in the kernel submit (bit-12 hold)
  reached from a draw (vk_queue_flush). memmove's main caller per the keyed rows: Turnip `tu_CmdBindPipeline`
  (vk_cmd_set_dynamic_graphics_state per draw). Not acted on yet.
- User (during/after my drag boots): "觸控有極大延遲極不跟手且出現花屏". The lag: my fuxitaps script drives finger 0 and the
  profilers add load. Clean boot `native-layout0019-user-boot-1` (loader `native-profpc-user-loader`: no script, no profiler,
  `fuxiperf=2`, 480 s, `boot-exp.ps1 -Reset 8` now supported). User: touch lag gone, smoother than before the task, but
  rendering errors (photos IMG_0781-0786 in Downloads; vertical green/magenta lines are a broken panel, ignore):
  lock screen date line / battery / home bar garbled; App Library background black; Settings header black; swipe into App
  Library = black with blue blocks (stale pieces of earlier frames); Today view black, widget panel corners cut wrong;
  Control Center looks like a heat map.
- Every broken element is framebuffer-fetch blending (vibrancy, material tints). Diagnosis (inference, consistent with
  the photos): patch 0003 removed rasterization-order (FLUSH_PER_OVERLAP) from non-fetching pipelines, so a fetching draw
  reads colors earlier draws left in the color cache: stale memory / zeros. Upstream's fetch_split adds a by-region
  color-write -> input-read barrier for exactly this; 0003 did not. 0003 gained only a few percent.
- Action: 0003 removed from `native-metal-check.yml` (harness bb216c4, iOS CI 43522235051) -> image 0020 = 0004 + 0005
  only (verifier `verify-ci-artifact-0020.py` asserts FUXI_M2V_ORDER_ALL absent). To be checked by the user.
- Image 0020 = 0019 - patch 0003 (bridge from iOS CI 43522235051, artifact 14083307923), staged on
  `D:\fuxi-images\native-layout0020-overlay.qcow2` (backed by the 0019 raw), exported `D:\fuxi-images\native-layout0020-root.img`
  sha256 `a4cc3e52847e64780b5b87013537110ab1ba163fbb968985cb88b477c965e075`. Deployed: 269 blocks (1076 MiB), phone before
  `1acb1f58...198f` (0019 after its boots), writer `61f128d3...115d`, supervisor `f5c8ec26...e55d`, status 0,
  READBACK_MATCH. **Phone root: 0020.**
- Clean boots of 0020 for the user's visual check: `native-layout0020-user-boot-1` (07:21-07:29) and `-boot-2` (07:31-07:39),
  both with ZERO touch contacts (user away). NOTE: `boot-exp.ps1 -Reset N` is the reset deadline in MINUTES (monitor
  "reset deadline: +8 min"), independent of `-Seconds`; boot-2's -Seconds 1500 still reset at 8 min. For a long visual
  window pass e.g. `-Seconds 1800 -Reset 30`. 0020 material check still pending.

## 2026-11-14 ~08:00 where the GPU time goes (0019 dragreg IBPROF re-read) and patches 0006-0008

- The display thread is NOT CPU-bound overall: 0019 profpc boot, t541 48 % of samples idle in its run loop
  (`IOMFBServer::thread_body` -> mach_msg wait), on-core only ~20 %. Its WaitFence/submit waits: submit_split total
  625 us/submit, backend 72 us on-core, retry_sleep 548 us (refusals busy 362 / hold 216 per 600). The fence waiter
  thread `metal2vulkan-queue-wait-0` is mostly asleep in WaitFence (`delay(100)` sleeps here: state=wait 28.7 %, run 8.3 %).
- GPU: per job (~7.5 ms span) the FIRST full-screen IB (ib 1) carries ~7.7 Mpx (2.9 screens) of linear->tiled 2D copies
  plus one full-screen UBWC clear and takes 4.3-5.3 ms; the other full-screen IBs carry 0-225 Kpx of copies and
  take 0.5-1.5 ms. Least squares over 478 full-screen IBs: us = -678 + 0.571 * blit_Kpx + 1.7 * draws. So those copies
  (runtime shadow copy-ins of buffer-backed textures before the pass, each followed by `Barrier::everything()`,
  17 WFI / 60 CCU events per IB) are the single biggest GPU cost. Which textures: unknown -> diagnostic patch 0008.
- All passes are sysmem (2D CP_BLIT clears into dest `0x1330` = 8888 UNORM, TILE6_3, UBWC). Upstream defaults
  `TU_DEBUG=sysmem,nocb` for FuxiMetal because of A740 BV/BR stalls with GMEM/concurrent binning
  (docs-lanes/metal-m9m.md:15-25, gpu-translation.md:1070-1081); GMEM would make the clears free but is a hang risk.
- Turnip prints `TU: info: FUXI KMD: submit IBs N fence N` for every submit (16073 lines in the dragreg boot).
  Mesa honours `MESA_LOG_LEVEL` (strings `warning`/`info`/`debug` in the dylib).
- Patches (harness ff67297, iOS CI 43530541576), generated against the exact as-built 0020 source (tarball + bridge
  0001-0007 + adrenometal 0009-0015 + fuxi 0001/0002/0004/0005, rebuilt in scratchpad `m2vb`, LF only); host
  `cargo check -p metal2vulkan-runtime` clean (Windows needs a stub for the unix-only `foreign_acquirer`):
  - 0006 keep-draw: `RenderCommandEncoder::keep_draw` decided after `source.kept()` (Arc clone of the index buffer);
    on the phone no instance keeps draws (`keeps_whole` false, extended shadows need `!fetch_inputs`), so each draw
    cloned and dropped it; 289 live samples (5.1 % of the display thread) sat on that atomic decrement
    (`keep_draw+0x1e0`, ldxr/stlxr loop). Now returns before taking it; same outcome.
  - 0007 mesa-log-level: `open_loader` sets `MESA_LOG_LEVEL=warning` on iOS unless set.
  - 0008 shadow-log (`FUXI_M2V_SHADOW_LOG`, default on for iOS): `FUXI M2V: shadow made <call> <fmt> WxH offset rows
    usage: <why>` for shadows >= 64x64 (first 400), and every 2000 copy-ins `FUXI M2V: shadow copy-ins N texels K
    images M top WxH/r<rowlen>[s] xN K ...`. Verifier `verify-ci-artifact-0021.py`.
- iOS CI 43530541576 success at ff67297 (artifact 14087891363, zip `073fa9ea...e204`, only the bridge changed),
  verified by `verify-ci-artifact-0021.py`. Image 0021 = 0020 + 0006/0007/0008, staged on
  `D:\fuxi-images\native-layout0021-overlay.qcow2` (backed by the 0020 raw), exported `native-layout0021-root.img`
  sha256 `3161f8366b9fed450abe82ad49dcb7c281981dc3da171408481a7c3da1bbd571`. Deployed: 194 blocks (776 MiB), phone before
  `67d501ff...f11d`, writer `351b9d29...dcac`, supervisor `2aed4ed2...894b`, status 0, READBACK_MATCH. **Phone root: 0021.**
  Drag boot `native-layout0021-dragreg-boot-1` (same loader/kernel as 0019's dragreg boot) started.
- Drag boot `native-layout0021-dragreg-boot-1` (loader native-ibprof3-dragreg-loader, kernel-ibprof3-artifact):
  backboardd 83.4 Hz, 56 % single flips, shown 63.2 fps, gpu_ms 7.99, span mean 8044 us (0019: 83.4 Hz / 56 % / 7491 us),
  no hang/lost. `FUXI KMD: submit` lines: 0 (0007 works). eprintln output arrives in fragments, one |backboardd| line
  per write: reassemble with `E:\...\reassemble-m2v.py <boot.log>` (output `native-layout0021-dragreg-boot-1-m2v.txt`).
- **0008 result**: steady per 2000 copy-ins (~ 33 frames): 1271x1823 x57 (2.3 Mpx each), 1326x1326, 1326x1160, 1105x1160,
  650x683, 465x465 (all BGRA8Unorm, `new_linear_texture`, ShaderRead) copied once per frame each = ~7.5 Mpx/frame,
  plus small A8Unorm glyph/text textures and 180x180 RGBA8 icons. Every shadow's reason: "the buffer is a no-copy
  buffer, and the device makes no linear image of the texture: its linear image for host allocations lays rows
  <align(width,64 texels)*cpp> bytes apart ...; and the device's image format query has no DRM_FORMAT_MOD_LINEAR image".
  CA's rows were 16/32-aligned (5088 for 1271, 5312 for 1326, 4432 for 1105, 2624, 1888, 720), Turnip's 5120/5376/
  4608/2816/2048/768. The runtime answered 16 for minimumLinearTextureAlignmentForPixelFormat: and the private
  `deviceLinearTextureAlignmentBytes` / `linearTextureAlignmentBytes` / bridge `deviceLinearReadOnlyTextureAlignmentBytes`.
- Patch 0009 linear-align (harness 2dea0be, iOS CI 43534717776): on iOS (`FUXI_M2V_LINEAR_ALIGN`, 0 disables) the
  public query answers 64 texels x texel bytes (`buffer_texture::texel_bytes`, now pub(super)) and the two private
  counts 256; IOSurface alignments and the runtime's placement rule LINEAR_ALIGNMENT=16 unchanged. Hypothesis (to be
  measured with 0008 still in): CA lays its decoded images at the answer, the textures alias, the copies go away.
- iOS CI 43534717776 success at 2dea0be (artifact 14089875032, zip `aa04f273...3ad3`, only the bridge changed), verified by
  `verify-ci-artifact-0022.py`. Disk: D: had only 19 GB (each raw image really takes 20 GB) -> the first export failed
  (qemu-img I/O error, empty file). Freed space losslessly: `native-layout0018-root.img` -> `native-layout0018-root.qcow2`
  (12.4 GB, `qemu-img compare` "Images are identical"), raw removed, `native-layout0019-overlay.qcow2` rebased (-u) onto the
  qcow2. Do the same with the next-oldest raw before the next export (D: 6.7 GB free after 0022).
  Image 0022 = 0021 + 0009, `native-layout0022-root.img` sha256 `6d26e1c071842ee14a4293ed1676b203058e69bbd4ca76fbfd67b95d751e3a8b`.
  Deployed: 165 blocks (660 MiB), phone before `91ec2ef3...61a9`, writer `eab51acc...8e2a`, supervisor `aa817163...76b6`,
  status 0, READBACK_MATCH. **Phone root: 0022.** Drag boot `native-layout0022-dragreg-boot-1` started.
- Drag boot `native-layout0022-dragreg-boot-1` (same loader/kernel): **backboardd 90.1 Hz (83.4), 71 % single flips (56 %),
  shown 68.3 fps (63.2)**, gpu_ms 7.61, span mean 7509 us (8044), boosted 22.8 s, max 67.8 C, no hang.
  0008 log: the big BGRA shadows are gone (5 shadows made in the whole boot; per 2000 copy-ins ~31 Mtexels instead of
  ~460: only two A8Unorm 242x134 rows 256 (Turnip also 256, refused for another reason, not read yet) and 256x1).
  `ib-compare.py`: ib1 (the 47-draw first full-screen pass) copies 7536 -> 69 Kpx, WFI 31 -> 9, CCU events 113 -> 27,
  but time only 5557 -> 5076 us. So the earlier regression over-attributed: ib1's time is mostly its draws
  (~110 us per draw, sysmem, FLUSH_PER_OVERLAP on every pipeline). Per job now: ib1 ~5.3 ms, quarter-res blur passes
  284x611 ~0.4 ms, two more full-screen passes 0.7-0.9 ms each. All full-screen clears target UBWC images (`d2 0x1330`):
  CA renders into private offscreen targets first.
- Disk: converted `native-layout0019-root.img` to a compressed qcow2 (`qemu-img convert -c`, 6.3 GB, "identical"),
  raw removed, `native-layout0020-overlay.qcow2` rebased onto it. (An uncompressed attempt on D: ran out of space and its
  partial output was deleted; the raw was re-hashed unchanged before.) D: 20.4 GB free.
- Visual check without the user: `boot-exp.ps1` / `run-monitor.ps1` got `-TourShots` (monitor `--tour-shots`); loader
  `native-tour-user-loader` = native-profpc-spin-loader with the user-loader swaps + `fuxitour=40 fuxitourhold=30`
  (kernel fuxi_keys.c fk_tour: lock, home, app-library x3, today, spotlight, control-center, settings, general, about,
  clock, calendar, photos; PNGs `framebuffer-shotN-NAME.png`). Boot `native-layout0022-tour-boot-1` (-Seconds 660 -Reset 14).
- Tour boot `native-layout0022-tour-boot-1` result (09:11-09:22, no hang, monitor_exit=0): **materials OK on 0022** -
  lock screen (date/time/battery/home bar clean), App Library (frosted folders over blurred wallpaper), Today (blurred
  background, widgets), Control Center (module backgrounds, sliders) all render correctly, i.e. the 0019 breakage was 0003
  and is gone. Shots 9-14 (settings/general/about/clock/calendar/photos) all show the HOME screen: the tour taps the stock
  iOS 16 layout (Settings 670,1710; Clock 670,1140; Calendar 410,860; Photos 670,860) while this root's page 1 has Photos
  (148,285), App Store (410,285), Settings (148,570); no `exec Preferences` in the log. Not an app-launch failure. For
  app shots use `fuxihuman=S fuxitourhold=H` (this root's layout) or fix the tour coordinates.

## 2026-11-14 ~09:45 GMEM retried on 0022 (copies gone): a clear win on the drag load, no loss

- Why retry: the night-of-11-13 `fuxigpugmem=1` trial (0016) ran while every pass still did IOSurface copy-in/copy-back
  (fixed by 0004) and ~7.5 Mpx/frame of shadow copies (fixed by 0009), which are sysmem blits either way; it also lost the
  device in loop 4. With those gone, ib1 (47 draws) is draws, and full-screen passes cost ~0.7-0.9 ms each in sysmem.
- Loader `native-ibprof3-draggmem-loader` = the 0022 dragreg args with `fuxiregtrace=1` (IORegistry trace, not GPU) ->
  `fuxigpugmem=1` (frontend sets TU_DEBUG=nocb: Turnip autotune, no concurrent binning). Built from native-ibprof3-spin-loader
  with the dragreg swaps; payload `41760492...06cc`, boot.img `85f98544...2b9e`. Boot `native-layout0022-draggmem-boot-1`
  (09:43:43, 240 s, kernel-ibprof3-artifact), nobody at the phone.
- `script-metric.py` (8 loops), sysmem 0022 dragreg -> GMEM:
  backboardd **90.1 -> 101.9 Hz**, gpu_ms **7.61 -> 6.20**, single-period flips **71 % -> 89 %**, shown 68.3 -> 76.2 fps
  (the script span includes idle gaps: shown is demand-limited; a mid-script SWEET FB window: 600 frames in 5461 ms =
  110 fps), boosted 22.8 -> 58.9 s, max 67.8 C both. No DeviceLost / NMB VK ERROR / runtime-failure / hang.
- IBPROF (`ibprof-summary.py`): job span mean 7509 -> 6188 us. Per job now: ib1 3.2 ms (47 draws x 9 window scissors =
  423 draw packets, 24 Mpx of per-bin clears), ib11 1.3 ms (27 draws), ib21 1.0 ms (7 draws), quarter-res passes ~0.2-0.35 ms.
  Not a flat per-draw cost: ib21->ib11 +20 draws = +310 us (15 us each), ib11->ib1 +20 draws = +1.9 ms (95 us each), so a
  few specific draws in ib1 are expensive in either mode (sysmem ib1 was 5.1 ms, its 23-draw variant 2.8 ms). Unknown which.
  The 1125x2436 4-draw passes that stayed sysmem take ~1.5 ms (were ~0.7-0.9 ms): to be read. Scanout copy avg 1119 us.
- User report (2026-11-14 ~09:57): "right after boot, at the lock screen, touch does not respond; I have to wait a while
  before swipe-up works". Cause found in `fuxi-h1/fuxi_touch.c` ft_thread: `delay_s = 30` (boot-arg `fuxitouchdelay`,
  no rationale in docs-lanes/touch-display.md or patch 0091) before the chip reset and sensing. 0019 user boot (launchd
  clock): SweetHIDTouch opened by the event system 39.1 s, first swap 41-42 s, `FUXI TOUCH: sensing` ~53 s and the
  first `touch 0 down` right after it (the user was already swiping): ~10 s of dead touch on the lock screen.
  New user loader `native-gmem-user-loader` = native-profpc-user-loader swaps + `fuxigpugmem=1` + `fuxitouchdelay=10`
  (sensing ~33 s, before the event system opens the device; reports before that have no client). Args 580. Not booted yet.
- **GMEM tour (`native-tour-gmem-loader`, boot `native-layout0022-tourgmem-boot-1`, 09:53-10:05): rendering CORRUPTED.**
  Lock, App Library (x2), Today are pixel-identical to the sysmem tour (`shot-compare.py`: mean 0.00-0.67), Control
  Center differs only by the volume level, Spotlight by a keyboard tip card (state). But shot 2 (home after unlock) and
  shots 11-14 show large horizontal bands of stale content - Control Center modules, Settings rows (Notifications,
  Background App Refresh, Photos, Wi-Fi Address), dock icons smeared - sheared as if rows were read with the wrong pitch;
  the sysmem tour's same shots are clean. Reading (inference, not verified): contents of tile memory / of another pass
  show where CA expects preserved pixels - a DONT_CARE load/store that sysmem happens to honour as "keep", or a missing
  pass-to-pass dependency that sysmem's direct writes hide. GMEM is NOT usable until that is found. No DeviceLost.
- User loader rebuilt WITHOUT GMEM: `native-touch10-user-loader` = native-profpc-user-loader swaps + `fuxitouchdelay=10`,
  `fuxigpuncimport=308808` (value 8 = one-shot SweetFramebuffer frame log; harmless). Args 566. The GMEM user loader
  was deleted before use. User request noted (low priority): capture the real iOS Apple logo frame and show it from the
  loader/kernel early, keeping the verbose text drawn on top of it.
- Boot `native-layout0022-touch10-user-boot-1` (10:20:18, `native-touch10-user-loader`, -Seconds 900 -Reset 16,
  -MaxStartTemp 450 because this boot is for the user's eyes, not a measurement): `FUXI TOUCH: starting in 10 s`,
  `sensing` at launchd ~41 s, right after `SWEET FB: swap 1` (41 s) and before the lock screen's later swaps; in 0019 it
  was ~53 s. (So the touch thread itself starts ~31 s on that clock, not ~23 s as estimated from the 30 s case.)
- Front-end patch for the GMEM bisect: harness `1756796` adds `patches/fuxi-tudebug-bootarg.patch` (tools/fuxi-metal/
  ClientSetup.hpp: `fuxitudebug=<flags>` = TU_DEBUG default for every admitted process, one token of [a-z0-9_,], an
  explicit env value still wins, prints `FUXI MTL: TU_DEBUG default from boot fuxitudebug=...`; ClientSetupTests extended)
  and applies it in native-metal-check.yml after native-ca-smoke-class. iOS CI 43545236161 (43545199315 was dispatched
  by mistake at the old commit and cancelled). Turnip in the image has every needed flag (strings in
  libvulkan_freedreno.dylib): nocb forcecb gmem sysmem nobin forcebin noubwc nolrz flushall syncdraw 3d_load
  noconcurrentresolves noconcurrentunresolves nobinmerging log_skip_gmem_ops, plus TU_DEBUG_FILE support.
  Bisect loaders ready (tour, hold 12 s, no fuxiperf): `native-tour-tux-loader` fuxitudebug=nocb,flushall,syncdraw;
  `native-tour-tuy-loader` fuxitudebug=nocb,noconcurrentresolves,noconcurrentunresolves. Verifier for the artifact:
  `xiaomi13-layout-backport-20261112\verify-ci-artifact-0023.py <run> <commit>`.
- metal2vulkan already models Metal's DontCare load (clear_reach.rs: tiles no primitive touches keep their texels; the
  runtime loads and clears only drawn tiles), so a plain DONT_CARE-load explanation of the GMEM bands is less likely.
- Disk: E: 0.2 GB free; D: 20.4 GB. 0020 raw being compressed to qcow2 (lossless, compare) with 0021's overlay rebased.

## 2026-11-14 ~10:40-10:50 disk cleanup and relocation (user: "清理下磁碟", "大檔案一律只能放到工作區文件夾內", "把c e的東西移動到工作區集中", "你自己判斷 保留重要資料")

- Deleted (superseded; user chose "C: and E: old images all"): on E: 29 files - layout 0011-0017 raws, native-* candidate
  roots (caller-smoke/-survey, nocopydiag, precaller-replayed-verify, priority, provenance-control/-survey v1-v3, survey,
  uchar3), all native-phone-pre*/post* backups incl. the 8.2 GB partial, dyld-extract-scratch.tar (16 GB, unreferenced;
  `dyld-cache\` used by dsc_sym.py is kept); on C: 8 early smoke-phase images (native-smoke/2/3-root, native-ca-root,
  native-phone-presmoke2/-retry1/presmoke3/preca backups). Also: Rust build cache
  `compressed-copy-span-prototype\m2v-work\target` (9.9 GB), `provenance-survey-transfer-chunks`, the five intermediate
  overlays 0018-0022 (each has a full image), old CI artifact dirs/zips 0011-0022 (verification JSONs kept), the previous
  session's scratchpad build dirs.
- 0020 raw -> compressed qcow2 (`qemu-img compare` identical); 0021's overlay had been rebased onto it.
- **Everything of this project now lives under `D:\xiaomi13\fuxi_ios\work\` (same folder names; `/work/` added to D:\xiaomi13\fuxi_ios\.gitignore):**
  `Codex-Xiaomi13-GPU-20261109\` (was E:\), `xiaomi13-ios-gpu-20261108\` (was %USERPROFILE%\Documents\Codex\; this file),
  `xiaomi13-layout-backport-20261112\` (harness worktree), `xiaomi13-gpu-research-20261111\`,
  `fuxi-scheme-comparison-20261112\`, `fuxi-images\` (was D:\fuxi-images; 0018/0019/0020 qcow2, 0021/0022 raw),
  `m2vb\` (as-built metal2vulkan source of 0020-0022, from the old scratchpad), `ci\ios-artifact-0023` + its zip.
  229 hard-coded paths in .py/.ps1/.sh/.bat/.cmd were rewritten (`Codex-Xiaomi13-GPU-20261109\rewrite-paths-20261114.py`);
  logs, evidence and the text above are NOT rewritten: read old paths through this mapping. Git worktrees repaired
  (`git worktree repair`). The empty folder E:\Codex-Xiaomi13-GPU-20261109 is left for the user to remove (a safety check
  blocked rmdir). Rule from now on: large outputs only under D:\xiaomi13\fuxi_ios\work, cleaned when done; nothing on C:.
- Frame log of the user's touch10 boot (sysmem 0022, `flog-summary.py native-layout0022-touch10-user-boot-1`, 128 swaps
  of real use): swap interval p10/p50/p90 7988/10058/13582 us (producer ~100 Hz); swap->copy_submit 4724 us mean (the copy
  waits for the frame's GPU job; NativeMetal has no writer lease, swap->ready 0); copy_submit->fence 1020 (blit 621 us);
  fence->START 2609 mean; swap->START (shown) p50 8466. 36 shown gaps of >=2 TE: 31 "copy/start late" (next swap 2.4-5.8
  ms after the previous START, then +4.3-5.2 ms GPU, +0.6-1.2 ms copy -> past the next TE), 5 producer-late. SwapWait is
  polled non-blocking (flags 0x80000000): of 160 2+ intervals only 25 followed a Busy return, so SwapWait is not what
  paces CA; `fuxiearly` 1/2 (SwapWait retire at copy queue / admission) therefore not expected to help much.
  GMEM raised backboardd 90 -> 102 Hz, i.e. the producer cadence is tied to GPU time (serial CPU->GPU->copy chain).
- cpus=7 is NOT the X3: the missing core is cpu1 (A510, hosts the loader's USB monitor); logical 6 = cpu7 = X3, inside
  fuxicores=0x7c (docs-lanes/cooling.md:460-470).

## 2026-11-14 ~10:24-11:00 image 0023 = 0022 + front end with `fuxitudebug=` (for the GMEM bisect)

- iOS CI 43545236161 success at harness 1756796 (artifact 14093518625, zip `771fa036...e159`), verified by
  `xiaomi13-layout-backport-20261112\verify-ci-artifact-0023.py`: marker in FuxiMetal.bundle/FuxiMetal; changed vs 0022:
  only FuxiMetal.bundle/FuxiMetal (+ build log and source tarballs). Bridge and FuxiNativeMetal unchanged.
- Share `D:\xiaomi13\fuxi_ios\work\ci\native-layout0023-share` (0022 share's scripts + mesa, `native\` = the verified artifact), overlay
  `D:\xiaomi13\fuxi_ios\work\fuxi-images\native-layout0023-overlay.qcow2` over the re-hashed 0022 raw (`6d26e1c0...3a8b`),
  `guest-run.py ... stage-test.sh INSPECT_SUCCESS` -> `native-layout0023-stage.log`: GUEST_OK, fsck OK x3.
  Export `native-layout0023-root.img` sha256 `24cce13b71e14635cc92c4b4c5ea1249423e50ef0f2dd0e6ebc53e891198c843`.
- Phone before the write (0022 + its boots): `6338ad3ffbb268ceb90ab6b57edaed4a67b37068f1f4a3f004d12452d6b72798`.
  Writer `native-layout0023-write.sh` `11025230...2ced` (0022's, stage path changed), supervisor `73db9d51...d7a8`.
- Bisect loaders rebuilt with `fuxitourhold=25`: a capture takes ~16 s (512-byte USB reads), a shorter hold would let the
  tour move on mid-capture.
- **0023 deployed** (11:00-11:06): map -> verify-native-map OK; `delta-stage-hash.py --name layout0023`: 294 differing
  blocks (1176 MiB), delta `8a287fbc...ffb5`, STAGE_VERIFIED `24cce13b...c843`; supervisor status 0, 24.1 s; unmap/remap
  readback `24cce13b...c843` = READBACK_MATCH. **Phone root: 0023.** Phone temp files removed, unmapped. (Note: from Git
  Bash, `adb push x /data/...` is rewritten to `C:/Program Files/Git/data/...` by MSYS path conversion - use PowerShell or
  MSYS_NO_PATHCONV=1 for adb paths.) Host: 0023 overlay removed (raw kept), 0022 raw being compressed to qcow2.

## 2026-11-14 ~11:08 GMEM bisect boot X (`native-tour-tux-loader`, fuxitudebug=nocb,flushall,syncdraw) on 0023

- `|backboardd| FUXI MTL: TU_DEBUG default from boot fuxitudebug=nocb,flushall,syncdraw` - the boot argument works. NOTE
  this replaces the whole default, so X is GMEM (autotune) too.
- Still corrupted: shots 2 (home), 3/4 (App Library), 6 (Today) carry full-width horizontal bands of stale content
  (Settings rows "Focus", "Screen Time", "General" - Settings was never opened this boot, i.e. an old app snapshot's
  pixels - App Library pieces on the home shot, Today widgets, dock icons smeared vertically). Lock, Spotlight, Settings
  (home) shots identical to sysmem. Control Center itself is clean, but its blurred backdrop is grey at the bottom: CA's
  own snapshot of the layers below already contained the bad pixels -> the corruption is in what CA renders on the GPU,
  not (only) in the kernel's scanout copy.
- Ruled out while reading: probe request 0x4e reads the buffer DPU SSPP SRC0 currently scans (not a fixed A), so shots
  are what was on screen; the kernel copy re-emits g4_setup (CCU_CNTL2/CCU_CNTL sysmem config, 2D formats) every copy;
  SweetFramebuffer copies whole frames (no dirty rows; sweetsurfrow is the IOSurface row alignment, 256 B).
- flushall+syncdraw did not help -> not a missing flush/sync. Full-width bands look like whole bin rows that were never
  rendered/stored (stale memory of a recycled surface, wrong pitch -> sheared). Next: `nocb,nobin` (GMEM without the
  hardware binning pass / visibility streams), then `nocb,noconcurrentresolves,noconcurrentunresolves`.
- Boot timing: the monitor renews the reset deadline every 600 s; keep -Seconds < 600 (tour ~480 s) to avoid idle minutes.
- Boot `native-layout0023-tunb-boot-1` (fuxitudebug=nocb,nobin; GMEM without the hardware binning pass): STILL
  corrupted - home shot has bands of Settings rows (Wi-Fi...) and Spotlight's "Siri Suggestions" over the home page. So the
  binning/visibility streams are not the cause. (Its App Library shot shows jiggle/edit mode; wavy icon edges there are
  the animation during the 16 s capture, not corruption.) Next: Y = nocb,noconcurrentresolves,noconcurrentunresolves.
- **Boot `native-layout0023-tuy-boot-1` (fuxitudebug=nocb,noconcurrentresolves,noconcurrentunresolves): shots 1-3
  pixel-identical to the sysmem tour (shot-compare mean 0.00).** The GMEM corruption is Turnip's a7xx concurrent
  resolves/unresolves (bin stores/loads overlapping later work), not binning, flushes or the kernel copy.
  All 14 tuy shots checked: lock/home/App Library identical to sysmem; shots 9-14 (the ones corrupted under plain GMEM)
  clean (mean 5.6 = global brightness); Today/Spotlight/CC differ only by state (a widget not yet loaded, keyboard tip,
  volume). No DeviceLost. Next: which flag is needed alone (tours `native-tour-tuun-loader` = nocb,noconcurrentunresolves,
  `native-tour-tures-loader` = nocb,noconcurrentresolves; -Seconds 290 -Reset 5, first 6 shots suffice), then the drag
  measurement of the fix.
- Boot `native-layout0023-tuun-boot-1` (nocb,noconcurrentunresolves only): shot 2 (home) corrupted again (Settings rows).
  The user unlocked the phone during this boot (testing touch), so shot 1 shows home; it is clean. -> unresolves alone are
  not the cause; testing noconcurrentresolves alone next.
- User report 11:4x "lock screen not touchable at first, recovers later": that boot was a tour loader WITHOUT
  fuxitouchdelay (sensing 53 s). Timeline (tuun boot, launchd clock): 42 s display on, swaps 1-3 (iOS logo), 42 s display
  off (iOS keeps TouchDetectionMode 0xfe = "display off, tap-to-wake"), 52 s display on + TouchDetectionMode 0x1 + lock
  screen frames (swap 6), 53 s touch sensing. iOS itself ignores touches until ~52 s; with fuxitouchdelay=10 the driver is
  ready at ~41 s, so the lock screen should take input from the moment it appears. From now on test loaders also carry
  fuxitouchdelay=10 so the user's checks are not confounded.
- **Boot `native-layout0023-tures-boot-1` (nocb,noconcurrentresolves only): shots 1-4 pixel-identical to sysmem.
  Root cause of the GMEM corruption: Turnip's concurrent resolves on this A740 setup** (bin stores overlapping the next
  bin's rendering leave rows of a recycled surface unwritten). Concurrent unresolves are fine.
  Fix: front end - `fuxigpugmem=1` sets TU_DEBUG default `nocb,noconcurrentresolves` (was `nocb`). Then measure the drag
  load with exactly the native-ibprof3-draggmem-loader args, so the only difference to the 101.9 Hz run is the flag.
- Cleanup: removed the bisect loaders (tux/tuy/tunb/tuun/tufl/tusd/tugm/tuub/tubm, tour-gmem, draggmem-flog, dragnobin; ~1 GB); boot evidence dirs kept. Recreate any with prepare-args-on-loader.py (swaps.txt in each evidence's continuation entry).

## 2026-11-14 ~12:05-12:25 image 0024 = 0023 + GMEM default without concurrent resolves; deployed

- harness 65ce7d7 (fuxi-tudebug-bootarg.patch: `fuxigpugmem=1` -> TU_DEBUG default `nocb,noconcurrentresolves`), iOS CI
  43554143453 success (artifact 14098220586, zip `128bd1fb...4003`), `verify-ci-artifact-0024.py`: string present in
  FuxiMetal.bundle/FuxiMetal, only FuxiMetal changed vs 0023. Share `D:\xiaomi13\fuxi_ios\work\ci\native-layout0024-share`, staged on
  an overlay over the re-hashed 0023 raw: GUEST_OK, fsck OK x3. `native-layout0024-root.img` sha256
  `f128ca55d5ea654e6e950837147bcb7f6f3d5286b95f339432bec6876b9d1641`.
- Deploy: phone before `d1ca36eb...221e` (0023 + bisect boots); delta 274 blocks (1096 MiB) `0168f7f4...e56a`;
  writer `505cad88...afa9`, supervisor `494292e0...b523`; status 0, 21.4 s; readback `f128ca55...1641` = READBACK_MATCH.
  **Phone root: 0024.** Phone temp files removed, unmapped. 0023 raw being compressed to qcow2; 0024 raw kept as base.
- Drag measurement started: `native-ibprof3-draggmem-loader` (same args as the 101.9 Hz GMEM run on 0022), boot
  `native-layout0024-draggmem-boot-1`.
- **Drag result on 0024 with fuxigpugmem=1 (= nocb,noconcurrentresolves)**, `native-layout0024-draggmem-boot-1`:
  backboardd **95.1 Hz** (sysmem 0022: 90.1; corrupt GMEM 0022: 101.9), gpu_ms **5.66** (7.61 / 6.20), single flips 76 %
  (71 / 89), shown 70.7 fps, boosted 29.4 s, max 69.8 C, no loss. IBPROF job span mean **5637 us** (7509 / 6225); the main
  pass ~1.5 ms mean. submit_split 309 us/submit (retry_sleep 226). GPU per frame ~5.6 + copy ~1.1 = ~6.7 ms < 8.26 ms,
  yet 95 Hz: the limit is no longer GPU execution - it is the producer / submit-copy serialization (see the frame-log
  analysis above). The corrupt GMEM run's higher Hz may have come from work skipped by the unwritten bins.

## 2026-11-14 ~12:30-12:55 (next session) GMEM "corruption" re-examined: the bisect was confounded by the user touching the phone

- Tour `native-tour-gmemfix-loader` on 0024 (`fuxigpugmem=1` = nocb,noconcurrentresolves, + fuxiperf=2 fuxicores=0x7c),
  boot `native-layout0024-tourgmemfix-boot-1` (12:31, -Seconds 580): shots 1, 2, 4, 10 show the same horizontal bands of
  other content; 6, 8, 9, 11-14 clean. Only differences to the clean tures boot: fuxiperf=2, fuxicores=0x7c, and
  fuxigpugmem=1 instead of fuxitudebug= (front-end code: gmem only changes the TU_DEBUG default, so same flags).
- BUT the boot log has 260 `SWEET HID: touch 0 down` (40 before shot 1: unlock swipe, random taps, Settings (150,575)
  opened ~15x with home swipes, Control/Notification Center): the user was using the phone during the tour.
  Touch counts of all tour boots: sysmem 0022 tour 27 (= the tour's own), tourgmem 289, tux 102, tunb 45, tuun 25 (+2
  before shot 1, user unlock), tuy 27, tures 11, this 260. Every "corrupted" boot had a user at the phone, every clean
  one had none; the concurrent-resolves flag does NOT separate them (this boot has it off and bands).
- Mechanism (inference): `save_framebuffer` reads 10 MB in 512-byte probe-0x4e requests over ~16 s, each from the buffer
  SSPP SRC0 scans at that moment (triple buffering). If the screen changes during the read, the PNG interleaves rows of
  different buffers/frames -> full-width bands, "sheared" moving content, content of other screens. Shot 10 here shows
  regular ~60-row stripes alternating keyboard / home. Also: during the 0022 GMEM tour (09:53-10:05) the user was at the
  phone and reported only the touch delay, no on-screen stripes.
- So "GMEM corrupts, noconcurrentresolves fixes it" is NOT established. The 0024 front-end default
  (`nocb,noconcurrentresolves`) cost ~7 Hz on the drag load (101.9 -> 95.1) for possibly nothing.
- Tooling: `fuxi-xnu-monitor.py --tour-verify` (run-monitor.ps1 passes it with -TourShots): every shot is read twice; a
  differing second read is saved as `-b` and logged `tour shot N verify moving rows_changed R span a-b` (a clock tick
  changes only rows above ~130), else `static`. Only static shots count as rendering evidence. boot-exp.ps1 now waits
  (Seconds+90) s for the monitor (was fixed 370 s: a 580 s tour made it exit early without the boot.log hash).
- Decisive test started: `native-tour-gmemplain-loader` (fuxitudebug=nocb = concurrent resolves ON, the "corrupting"
  setting; no fuxiperf; fuxitourhold=40 so two reads fit), boot `native-layout0024-tourplain-boot-1`, user asked not to
  touch the phone.
- Prepared (not booted): `native-inline-dragreg-loader` (= native-ibprof3-dragreg-loader, sysmem, with
  fuxigpuncimport 308800 -> 2405960 = + bit 21 inline present copy + value 8 frame log) and
  `native-inline-draggmem-loader` (same on the GMEM drag args). Why inline: in the classic path the present copy of frame N
  waits for an idle GPU (`fg_client_busy` clears only when every queued job retired) and, with bit 12, client submits are
  refused while it waits; Turnip's darwin backend then blocks in `dw_wait_fence_locked_free` (waits for the OLDEST job,
  turnip-darwin-kmd.patch darwin_submit_chunk), so CPU encode, GPU job and copy run as one serial chain. 0024 drag window
  stats: `ready_present_us` 5699 avg (the wait for idle), `queue held` ~1000 per window, copy 1035 us, poll_late 327 us.
  Inline (fuxi_gpu_abi_backend.inc:436-441, fuxi_adreno.c:889-905): the copy is emitted into the client's own in-order
  ring right away (window of the present root mirrored at 0x90000000/0xa0000000), no TTBR switch, client submits never
  refused for it; falls back to the classic GPU copy when the window/ring room is missing (not to a CPU copy). Ordering
  without a writer lease: CA calls flush(true) (commit + waitUntilScheduled) before the IOMFB swap, Turnip's vkQueueSubmit
  reaches the kernel synchronously (darwin_queue_submit -> dw_call; Mesa DEFERRED mode with no pending waits flushes in
  the caller), so job N is already in the ring when copy N is queued behind it. Upstream ran inline on device
  (O220/Q222/R223, gpu-scanout.md:1422-1520) but with the writer lease, i.e. copy N queued after writer retire, behind
  job N+1. Check in the frame log: `ahead` (client jobs in flight when the copy was queued) should be >= 1 for most copies.
- **Boot `native-layout0024-tourplain-boot-1` (12:44, fuxitudebug=nocb = GMEM with concurrent resolves ON, no fuxiperf,
  13 touches = the tour's own, nobody at the phone): shots 1-4 (lock, home, App Library x2) pixel-identical to the
  sysmem tour (mean 0.00-0.01), shots 6-8 differ only by state (Today widget, Spotlight keyboard shown, CC volume).
  Second reads: only small regions changed (rows 1591-1608, the clock rows 34-67, 1425-1501 on Spotlight = cursor).
  No DeviceLost / NMB VK ERROR / HANG.** The previously "corrupted" home shot is clean with the "corrupting" flags.
  Conclusion: GMEM renders correctly; the bisect's bands were capture artifacts of a moving screen (user at the phone).
  On 0024 use `fuxitudebug=nocb` for full GMEM (fuxigpugmem=1 now means nocb,noconcurrentresolves, ~7 Hz slower);
  the front-end default should go back to `nocb` in the next image. The user's eyes remain the final check.
- Inline drag boot started: `native-inline-draggmem-loader` rebuilt with `fuxitudebug=nocb` instead of fuxigpugmem=1
  (args 606), boot `native-layout0024-inlinegmem-boot-1`, kernel-ibprof3-artifact. Reference: 0022 draggmem (plain nocb,
  classic copy) 101.9 Hz backboardd, gpu_ms 6.20, single flips 89 %, shown 76.2.
- **Inline result, `native-layout0024-inlinegmem-boot-1`** (bit 21 + frame log, full GMEM): `FUXI GPU: inline copy window
  ready (buffers 0x7)`, inline_copies 2400+ with fallback 0, no DeviceLost/HANG. script-metric: backboardd **102.1 Hz**
  (classic plain GMEM 0022: 101.9), gpu_ms 6.11, single 87 % (89), shown 75.9 (76.2). Throughput unchanged.
  Frame log (128 swaps after the 3000-swap warmup, a busy window): swap interval mean 8614 us (p10 7409, p50 8517, p90
  10564) = ~116 Hz; copy queued 64 us after the swap (p50), `ahead` = 1 for all 128 (copy behind its own job: ordering
  OK), GPU submit_to_copy_start 6.0 ms, blit 0.6 ms, 124/128 shown, 9 two-TE gaps (8 copy/start late, 1 producer).
  Per-600-frame windows alternate ~110-116 fps and ~88-94 fps with the drag phase. In the slow windows backboardd's
  gpu_ms is ~6.5 but gpu_busy_est only 55-60 %: the GPU is NOT saturated there; the producer (display thread CPU, ~11
  ms/frame) is. Inline removes the copy wait from the chain but the chain was not the limit any more.
- Next: sweetprof on this config, `native-inline-gmemprof-loader` (= native-profpc-drag-loader with inline 2405952 and
  fuxitudebug=nocb, no fuxiearly/frame log; args 606), boot `native-layout0024-inlineprof-boot-1`, kernel-profpc-artifact.
- **sweetprof on inline + full GMEM, `native-layout0024-inlineprof-boot-1`** (103.0 Hz, same as without profiler):
  slides FuxiNativeMetal 0x109d8c000, libnative_metal_bridge 0x10c878000, libvulkan_freedreno 0x10e034000 (macho-fit on
  the PROF bt addresses; binaries from `D:\xiaomi13\fuxi_ios\work\ci\native-layout0024-share\native` and `...\mesa`).
  display.primary (t534): 30000 samples; run-loop wait (IOMFBServer::thread_body mach_msg) 6858 = 23 %; 9902 "dropped"
  (failed hash insertions: on-core samples with scattered pcs), so the thread is on a core most of the remaining ~75 %.
  CA renders from the vsync callback (vsync_callback -> render_for_time -> can_update -> SwapWait poll). Live on-CPU
  buckets (prof-pc, 9272 in the top buckets): by image platform 27.8 % (memmove 18.2, os_unfair_lock 7.2, memset 1.8),
  bridge 23.4 %, malloc 13.8 %, kernel traps 12.1 % (bucket at _mach_generate_activity_id 9.0 = inside mach traps),
  pthread 5.7, QuartzCore 5.4, Turnip 4.8, objc 4.1, FuxiNativeMetal 1.7. One keyed live row: memmove <- tu_CmdBindPipeline
  +0x240 <- render::Recording::record <- draw_resident (metal2vulkan already skips re-binding the same pipeline:
  `bound.pipeline`, render.rs:7808). So ~75 % of the display thread is translation-layer overhead, CA itself ~5 %.
- **Why the CPU side is slow now: thermal.** FUXI PERF during the script: `freq unboosted gold ~1402 MHz` most of each
  10 s, boosted (gold 2323 / prime 2592 max) only 1.2-5.7 s per 10 s, `max temp 647-671 dC`, `cuts` 2-7: with fuxiperf=2
  the policy is `hot 660 limit 720`, i.e. boosts are cut at 66 C. fk_swipe calls fuxi_perf_input() every 8 ms step and
  real touches on every report (fuxi_touch.c ft_report), so input is not what limits it. The "CPU frequency is not the
  bottleneck" reading of 11-14 01:xx was made while the GPU chain (8.7 ms) was the limit; with GPU ~6 ms it no longer
  holds. Raising the thermal threshold is the user's call (not done).
- Inclusive profiler (kernel): harness `codex/kernel-bo-slots` 63cd106 adds `patches/kernel-prof-inclusive.patch`
  (applied after kernel-prof-live in kernel-bo-slots-check.yml): for live samples of threads named
  com.apple.coreanimation.display*, every distinct 64-byte bucket of pc, lr and up to 24 frame return addresses counts
  once -> "PROFINC t<tid> 0x<bucket> <n>" (top 360) + "PROFINC done samples= dropped= frames=", 8192-entry table.
  Mirror xnu/fuxi-prof/sweet_prof_sample.c and 0111's embedded copy regenerated together (scratch repo
  `D:\xiaomi13\fuxi_ios\work\kprof-src`, LF). NDK clang -fsyntax-only -Wall -Wextra -Werror clean with XNU stubs, with and without
  SP_NO_LIVE. Kernel CI 43561278214.
- CORRECTION of the thermal reading above: per-10 s FUXI PERF `boosted` ms vs the mean backboardd Hz of the same 10 s
  shows no relation (corr -0.26 / -0.47 / +0.06 over the inlinegmem, inlineprof and 0022 draggmem boots; 10 s with 0 ms
  boost still gave 107-112 Hz). CPU frequency is not what makes the slow windows slow. (Thresholds not touched, user not
  asked.)
- Per-window timeline (inlinegmem): backboardd polls SwapWait on nearly every TE (swapwait intervals_1 575-623 of ~600,
  2+ only 1-33, few after Busy) but swaps only 91-96/s in the slow windows (110-118 in the fast ones, alternating
  about once per 8 s swipe). GPU ~63 % busy incl. copies in the slow windows. So backboardd often has nothing new to draw.
- **SpringBoard profile** (`native-inline-sbprof-loader` = same args, `sweetprof=10040`, `debug=12` and `fuxicores=124`
  written in decimal to fit 607 chars; boot `native-layout0024-sbprof-boot-1`, 102.5 Hz): main thread t243 wait 79 %
  in its run loop, live on-CPU 3180/30000 = 11 %; eventfetch 98 % wait, inProcessAnimationManager 85 % wait. SpringBoard
  is not the limit either.
- **The drag load itself is input-limited**: fk_swipe steps with a RELATIVE `fk_script_sleep(8)` after each
  sweet_hid_touch; the loops actually take ~21 s instead of the planned 16.1 s (600-frame window time between `FUXI
  TAPS: loop` markers, loops 2-8: 23.9/18.7/23.5/18.1/23.7/18.1/23.6 s), i.e. ~10.5 ms per step = ~95 Hz touch input,
  which is the slow-window rate. A real finger reports every 8.3-8.5 ms (`FUXI TOUCH: contact ... us apart` in the
  user boots: 8298-8762 us) = ~119 Hz. So the drag benchmark under-reports what a real finger gets in the drag phase.
- Kernel patch `kernel-swipe-pacing.patch` (harness 0c3bfad, after kernel-prof-inclusive in the workflow): fk_swipe on an
  absolute 8 ms grid (mach_absolute_time + assert_wait_deadline; a late step is not made up, no bursts), same line count
  in 0103-fuxi-kernel-h1.patch (hunk headers unchanged) and the fuxi-h1/fuxi_keys.c mirror. PROFINC got the profiled
  task's first thread too (harness 97358bc). Kernel CIs 43561574469 (97358bc) and 43562108724 (0c3bfad, both changes).
- Paced drag (`native-pace-draggmem-loader`, kernel-pace-artifact = CI 43562108724 at 0c3bfad, kernel sha256
  `9a9bd600...ceb7`; built as kernel variant of native-ibprof3-spin-loader -> `native-pace-spin-loader`, then the drag
  args with prepare-args-on-loader, because verify-kernel-variant-payload.py refuses sources with fuxiperf=/fuxiearly=):
  boot `native-layout0024-pacegmem-boot-1`: 9 loops in the script span instead of 8 (loops ~18 s: steps still ~9 ms when
  late steps are dropped), backboardd **102.5 Hz** (102.1), same fast/slow alternation (windows during the right-going
  drag mostly 90-97 fps, left-going 99-115). Input rate is NOT the limit either.
- **PROFINC** boot `native-layout0024-profinc-boot-1` (`native-pace-gmemprof-loader`, sweetprof=40, 101.3 Hz): 22088 live
  samples, all display.primary (t527); `prof-inc.py` (new) symbolizes; slides FuxiNativeMetal 0x1074cc000, bridge
  0x108e8c000, Turnip 0x10a648000. Inclusive share of the display thread's on-CPU time (denominator 22088; stacks are cut
  at 24 frames, recursive LayerNode::apply over-counts): CA MetalContext::draw 44 %, metal2vulkan RenderCommandEncoder::draw
  30 % (draw_resident 26 %, Recording::record 5 %), CA capture_backdrop 16 %, GaussianBlur/blur_surface ~10 %, CA CPU blur
  setup (BlurState::narrow_blur + process_edges) ~6 %, endEncoding 6 %, open_render 5 %, set_surface/prepare_destination 8 %,
  memmove 7 %, tiny_malloc 5 %, commit 3 %, mach_msg 3.6 %. Two rows are artifacts of bucket symbolization (a 64-byte bucket
  is named by its start, which can lie in the previous function): `-[NBCommandBuffer pushDebugGroup:]` 3.3 % (that method
  is an empty try/catch in NativeBridgeMetalFrontend.mm) and `__pthread_cond_updateval` 7.6 %. Kernel fix pushed: exact
  return addresses + call edges (`kernel-prof-edges.patch`, PROFEDGE lines), CI 43564376497 (9bbac97). Wait/ready samples
  (`kernel-prof-inclusive-wait.patch`, b7f462b, CI 43563536136) count off-core time of the same threads by class.
- **Wait-state PROFINC** (kernel-profwait-artifact = CI 43563536136, b7f462b; loader `native-profwait-gmem-loader`; boot
  `native-layout0024-profwait-boot-1`, 102.9 Hz): the class filter also caught display.wireless0-2 (idle, all W) and
  backboardd's main thread; display.primary t543: live 22275 of 30000 = 74 % on a core, W 6947 = 23 %, and the W
  samples are ALL the run loop (`IOMFBServer::thread_body` -> CFRunLoopRun -> mach_msg). No hidden lock/semaphore/page
  waits: the display thread is CPU-bound at ~7.2 ms per frame (incl. ~8 % in-kernel mach_msg). `__pthread_cond_updateval`
  is the bucket of the thread root (`_pthread_start`), an artifact.
- **Exact-address PROFINC + PROFEDGE** (kernel-profedge-artifact = CI 43564376497, 9bbac97; boot
  `native-layout0024-profedge-boot-1`, 101.5 Hz; slides FuxiNativeMetal 0x1036bc000, bridge 0x10507c000, Turnip
  0x106838000; `prof-inc.py ... --callees/--callers <fn>`): of 22426 live samples: CA draw_elements 44.7 %, m2v
  RenderCommandEncoder::draw 30.2 % (-> draw_resident 21.2 %: Recording::record 4.5 %, settle_draw 1.5 %, retain_draw
  1.1 %, memmove 0.8 %, ArgumentTable::retain 0.5 %, instantiate_into 0.5 %, rest inline in draw_resident itself), CA
  capture_backdrop 16.2 %, GaussianBlur 10.3 %, set_surface 8.5 %, endEncoding 6.6 %, memmove 6.6 %, CA
  BlurState::narrow_blur 6.2 %, tiny_malloc 4.7 %, commit 3.4 %, mach_msg 3.6 %. The pushDebugGroup / cond_updateval rows
  are gone (bucket artifacts confirmed). PROFEDGE dropped 6502 insertions (table full): only the larger edges are there.
- Next test: CPU frequency directly. `fuxiperfbase=MHz` (fuxi_perf.c:61): gold/prime vote while not boosted and not
  thermally blocked; blocked still drops to the cool 1.4 GHz level (protection unchanged, threshold not raised).
  Loader `native-pace-base2323-loader` = pace drag args with fuxiperfbase=2323 instead of fuxiearly=16, frame log off,
  and to fit 607: debug=12, fuxicores=124, fuxitaps=9 (loop 1 runs before the lock screen), unlock swipe ends at y=99.
  Boot `native-layout0024-base2323-boot-1`.
- Disk: D: had 1.5 GB free. `fuxi-images\native-layout0023-root.img` (21.5 GB raw) still exists next to
  `native-layout0023-root.qcow2` (the previous session's convert->compare->rm chain was cut by its usage limit);
  `qemu-img compare` now says "Images are identical", qcow2 check clean. Deleting the raw needs the user's OK (rule for
  files left by earlier sessions). This session's finished loaders and kernel-profwait-artifact (0.94 GB) removed.
- Boot `native-layout0024-base2323-boot-1` is INVALID as an A/B: every window 71-90 fps (backboardd 70.1 Hz, gpu_ms 5.06,
  late +67) while `freq unboosted gold` stayed ~1411 MHz (base LUT gold 2188.8 / prime 2227.2 MHz was announced but the
  SoC sat at 65-69 C, i.e. blocked, so the base vote hardly ever applied). The difference came from the script edits made
  to fit the args (unlock swipe to y=99, taps starting at 9 s): a different UI state. A frequency test needs the base vote
  not to be blocked at 66 C = a higher thermal threshold = the user's decision. Loader removed.
- **User loader `native-best-user-loader`** (native-profpc-spin-loader + sweetnoexec vmd, fuxigpuncimport=2405960 (inline
  copy + one-shot frame log), fuxiswapwait=0, fuxiperf=2, fuxicores=0x7c, fuxitouchdelay=10, fuxitudebug=nocb = full
  GMEM; args 584; boot.img `87a38000...c8b60f`), booted for the user's eyes: `native-layout0024-best-user-boot-1`
  (-Seconds 1800 -Reset 30). To check by eye: horizontal stripes / stale bands (GMEM), materials, smoothness.
- User (after the base2323 boot): "剛剛腳本滑動的是自訂鎖屏 那裡確實比會比較卡" (the script was swiping in the lock-screen
  customization screen, which is laggier). Log confirms: base2323 has `exec AegirPoster` x38 (152 AegirPoster lines,
  crash/respawn loop) while pacegmem / inlinegmem / profedge have 0 AegirPoster (their drags ran on the home pages).
  So base2323's 71-90 fps was the customization gallery + AegirPoster loop, not the base frequency (invalid A/B,
  confirmed). AegirPoster crash: sig 6, `abort <- std::terminate <- _objc_terminate <- _dispatch_client_callout <-
  FrontBoardServices __FBSSERIALQUEUE_IS_CALLING_OUT_TO_A_BLOCK__` = an uncaught exception in a FrontBoard serial-queue
  block; the reason string is not in the log (`reason ns 2 code 0x6: (none)`). Likely related to the earlier finding that
  poster extensions cannot open a Metal device on this port (unverified). Separate defect, not chased yet. Scripted loads
  must keep the 999 end point of the unlock swipe (y=99 lands in the customization screen) and fuxitaps=25.

## 2026-11-14 ~14:45 user: heat, goal redefined; GPU idle governor next (thermal threshold NOT raised)

- User: "可以調高溫度 但目前手機溫度已經高到手快抓不住了", "可以工作區文件你自己判斷該不該刪除就好", "目標是手機系統流暢可用
  而非單純桌面滑動", "目前是cpu 還是gpu在高溫啊?". Memory: xiaomi13-goal-system-usable.
- Which part is hot (best-user boot, monitor thermal lines every 10 s, user using the phone): hottest sensor GPU
  (gpuss-0/-2) in 40 of 52 samples, CPU in 9; averages gpu 53.9, gold/prime 53.4, cpuss 53.3, ddr 52.5, silver 52.1 C;
  peaks gold/prime 64.7 (boosts) vs gpu 63.3. The die shares the heat; battery was 42 C at boot (charging over USB).
- Cause candidate: `fuxigpulevel=8` is a static vote for the whole boot: GX rail voltage and the DDR row of level 8
  (6.07 GB/s) stay up even on a static screen (docs-lanes/gpu-clock.md:317-357). The kernel has an opt-in idle governor
  never run on the device: `fuxigpulevel=L|0x10|(H<<8)`, cap L, after H*10 ms (default 100) without a doorbell it votes
  level 1 (220 MHz, 2.1 GB/s row) and stops; a doorbell (client submit or scanout copy) votes the cap again; can only
  lower the vote; 8 failed HFI votes disable it. `fuxigpulevel=24` = cap 8, hold 100 ms. Log: `FUXI GPU DCVS: ...`.
- Not raising the CPU thermal threshold for now (the phone is already uncomfortable to hold; heat is part of "usable").
- Disk (user delegated the call): deleted `fuxi-images\native-layout0023-root.img` (dup of the verified qcow2) and the
  superseded 0018-0021 qcow2 images (no backing chains; restore points kept: 0022 qcow2, 0023 qcow2, 0024 raw = phone).
  D: 1.5 -> 60.3 GB free.
- Loaders: `native-pace-dcvs-loader` (pace drag args, fuxigpulevel=24; args 607; boot.img `5175d8a0...c498`) to check
  that the governor costs no frame rate; `native-cool-user-loader` (best-user + fuxigpulevel=24; args 585;
  `6302484d...5fc7`) for the user. Plan: drag A/B vs `native-layout0024-pacegmem-boot-1`, then a static-screen heat A/B.
- **User visual check of `native-best-user-loader`** (full GMEM + inline copy, 0024): "目前看起來應改有好一些
  雖然溫度依然偏高 我只有重複打開設定 並且在設定裡面頻繁滑動 以及頻繁滑動桌面" -> somewhat smoother, still hot; no stripes
  or rendering errors reported while opening Settings repeatedly, scrolling Settings and the home pages. That workload
  (open Settings, scroll it, go home, page the home screen) becomes the next scripted load instead of the slow drag.
- User (15:0x, after idling the phone): "又讓他待機了一下 溫度貌似沒有降下來 且目前都是些日常極低負載的場景 溫度不應該這麼高".
  best-user boot thermal timeline (monitor lines every 10 s, 1800 s): long idle stretches (600-840 s, 1260-1320 s,
  1440-1500 s, 1680-1740 s) still sit at 48-50 C SoC; CPU cluster clocks 1228.8/1401.6/1708.8 MHz in EVERY sample and
  FUXI PERF state `cool`, never idle. Three idle-power sources, all on even with a static screen:
  1. CPU: the governor's idle state is opt-in (`fuxiperfidle=ms`, 1000-60000: after that long without input or swap it
     votes the lowest LUT row >= `fuxiperfidlekhz`; never before the first swap; a swap/input wakes it). No loader had it.
  2. GPU/DDR: static `fuxigpulevel=8` (GX rail + 6.07 GB/s DDR row held). Opt-in idle governor `fuxigpulevel=24`.
  3. The loader's monitor core (cpu1, silver) busy-polls USB by default; the monitor has `--monitor-idle 0|1|2`
     (0 busy, 1 500 us delay, 2 WFE + timer event stream; docs-lanes/thermal.md:277,420). Now plumbed:
     `boot-exp.ps1 -MonitorIdle 2` -> run-monitor.ps1 -> `--monitor-idle 2` (needs --thermal-log, which we pass).
- `native-cool-user-loader` rebuilt: best-user + fuxigpulevel=24 + fuxiperfidle=1000 (args 603, boot.img
  `d4e79c04...a27a`). Not booted yet (the user wanted the phone back in Android).
- The user boot's reset deadline had been renewed at ~15:08 (monitor renews every 600 s), so the phone would have stayed
  in iOS until ~15:38. New `reset-soon.py [min]`: sends only the monitor's 0x54 request (deadline now + N min) after
  checking the FXIMON01 protocol; run with plain python (pyusb is a user-site package, not visible under -I). Used at
  15:17 -> Android back at 15:19 (AC powered, level 92).
- Kernel CI 43569078047 (67a88bf, `kernel-taps-presets.patch`: `fuxitapcmd=@1` = open Settings (148,570), scroll it 4x,
  home, page the home screen right/left; `@2` = the old slow drag) for a load that matches the user's real use.
- **Cool boot `native-layout0024-cool-user-boot-1`** (15:33, `native-cool-user-loader`, `-MonitorIdle 2`, user using it):
  all three active: `monitor idle mode 2`, `FUXI PERF: ready idle LUT 307200/499200/595200 kHz after 1000 ms`,
  `FUXI GPU DCVS: cap level 8 floor 1 hold 100 ms`, at 1000 s `ups 128 downs 128 fails 0 floor_ms 581562` (GPU at
  220 MHz ~58 % of the time), no DeviceLost/HANG/vote failure. Idle stretches: CPU 307/499/595 MHz, SoC 45.1-46.5 C after
  1-2 min idle (best-user boot: 48-50 C after many minutes at 1228/1401/1708 MHz). Note level 8's DDR vote in this
  kernel reads `bw_index=2 ddr_kBps=12449218`.
- User: "溫度有明顯降溫 但還在該場景感覺還可以再低 根本只是系統應用與頻繁滑動而已不應該還這個溫度與流暢度(還是會掉真)"
  (clearly cooler, should be lower still; frames still drop in system apps / frequent scrolling).
- **User-reported defect (photos `native-layout0024-cool-user-boot-1\user-photos\IMG_0789/0790.jpg`)**: lock-screen
  customization, COLLECTIONS gallery: every wallpaper card is black except the rainbow one (Pride, vector stripes, which
  renders). The coloured vertical lines in the photos are the broken panel. Same boot: `exec PridePosterExtension`,
  `exec UnityPosterExtension`, `CollectionsPoster` (2 execs, surfaces 1125x2436 created), `CRASH AegirPoster` x30 and
  `CRASH assetsd` x4, all sig 6 with the same abort <- std::terminate (uncaught exception) shape; 4x `FUXI LINEAR:
  foreign snapshot imported format=204 1125x2436`. Hypotheses (unverified): image-backed posters get their image via
  assetsd / an extension that throws, and with fresh-zero (0002) an unwritten texture now reads black instead of magenta.
- 30 min comparison (user using the phone both times): best-user (static GPU vote, no CPU idle, monitor busy-poll)
  monitor max-temp median 50.5 C / min 49.3; cool-user median 45.7 / min 44.6 (idle-clock samples median 45.3).
- User reference (Scene app on stock Android at 16:09, 4:46 after boot, charging 3.61 W, right after our iOS boot):
  GPU 220 MHz 0 %, CPU load 4 %, silver 672 / gold 614 / prime 864 MHz, CPU 42.2 C, battery 37.8 C. Our cool idle clocks
  (307/499/595 MHz, GPU 220) are already at or below Android's; the remaining few C are not clock votes (candidates: no
  deep CPU idle / power collapse in this XNU port, DDR, display path, charging; unmeasured).
- Run A `native-layout0024-usehot-boot-1` (`native-use-hot-loader`, kernel-taps-artifact = CI 43569078047, fuxitapcmd=@1)
  is INVALID: no `exec Preferences` at all, `exec Spotlight` instead; the user confirmed "腳本在桌面與搜索亂滑動". The
  Settings taps never opened the app (coordinates right: the user's own tap at 150,575 opened Settings 7x in the cool
  boot). Hypothesis: a scripted tap is one down report, 80 ms of silence, one up; a real finger reports every ~8.4 ms
  while down, and synthetic swipes (every 8 ms) always worked; the tours' app shots never opened apps either.
  The usage loaders also lacked fuxitouchdelay=10 (user: "且開機觸控有要等了").
- Kernel `kernel-input-fixes.patch` (harness 2ffeeeb, CI 43575185761): 't' taps and the tour's fk_tap become
  fk_swipe(x,y,x,y,96) (held in place, a report every 8 ms); touch sensing default delay 30 -> 10 s (fuxi_touch.c +
  0091 added line + 0093 context line, line counts unchanged). Run B not started (would repeat the broken script).
- Run A2 `native-layout0024-usehot-boot-2` (kernel-input-artifact = CI 43575185761; touch delay 10 s confirmed: `FUXI
  TOUCH: starting in 10 s`) again had no `exec Preferences`. Diagnostic `native-tapdiag-loader` (unlock swipe, 40 s,
  tap 148,570, 40 s; -TourShots): `native-layout0024-tapdiag-boot-1\framebuffer-shot1/2-step.png` show home page 1 =
  Photos, App Store, a Photos widget ("No content available"), dock Phone/Safari/Messages. There is NO Settings icon on
  page 1 any more (earlier shots had it at row 2 left); the tap hit empty wallpaper, so the scroll swipes then pulled
  Spotlight. The tap itself is not the problem. Both usage runs so far measured home scrolling + Spotlight only.
- User: Settings is on home page 2, row 1 col 1. Preset @1 changed to: home, page left, tap 148,285 (row 1 col 1, same grid
  cell as Photos on page 1), scroll x4, home, page right (`kernel-taps-page2.patch`, harness 2aae087, CI 43577539716).
- User asked for a KernelSU module to enter iOS by themselves, and whether this is dual boot or a temporary boot.
  Answer: temporary boot (the iOS root is a persistent managed image in userdata, but loader + XNU are `fastboot boot`ed
  from the PC into RAM each time; no boot partition is written). Facts for a host-free path: the loader launches XNU only
  on the host's USB request 0x49 value 0x584e (probe.c:1268) and otherwise resets at its 90 s deadline (probe.c:1472);
  the monitor also does touch power-up / monitor idle / reset deadline before the launch. Phone: slot `_a`, partitions
  boot/init_boot/vendor_boot/recovery/dtbo/vbmeta _a/_b (dedicated recovery_a/recovery_b), KernelSU ksud 3.2.4, kernel
  5.15.178-android13. Options put to the user: (1) loader autolaunch build + loader in recovery_a (backup first) + KSU
  action "verify map, unmap, reboot recovery"; (2) KSU action reboot-to-fastboot + a PC watcher that fastboot-boots the
  cool loader and runs the monitor (no partition writes). Waiting for the user's choice.

## 2026-11-14 ~17:10 user chose option 1 (standalone iOS boot from the phone, loader in recovery_a); started, NOT done

- User: "方式一 目前手機都有備份". Still back up recovery_a to D:\xiaomi13\fuxi_ios\work (sha256, readback) before any write.
- Host handshake the loader needs today (fuxi-xnu-monitor.py:278-425): 0x40 report, 0x46 status, 0x45 core_probe
  (CPU_ON cpu@100), 0x46 value 1 (MON_DELEGATE=1 -> probe_main sets 2 at EP0_SETUP -> probe_secondary_main sets
  MON_ACTIVE and polls USB), 0x4b UFS / 0x42 display snapshots (reads), 0x57 0x4d49 monitor idle (th_idle_mode,
  probe.c:994), 0x54 reset deadline, 0x55 0x5450 stage 9 touch_power (rails, reset, sense on; probe.c:717), then
  0x49 value 0x584e -> MON_LAUNCH=1 (probe.c:1268); probe_main returns when MON_ACTIVE && MON_LAUNCH (probe.c:1506),
  else reboot at the 90 s deadline (probe.c:1472). The secondary resets the SoC at MON_DEADLINE (probe.c:797).
- Build check: the deployed loader objects `xiaomi13-ios-gpu-20261108\cpu-fallback-gpu-c-loader\{start,loader,console,
  probe}.o` are reproduced section-for-section (.text/.rodata/.data/.bss identical) from the CURRENT D:\xiaomi13\fuxi_ios\ios-fuxi
  shim sources (incl. its local loader.c edits) with NDK 26 clang 17 and build.sh's flags WITHOUT
  `-Wno-error=unterminated-string-initialization` (clang 17 rejects it). Scratch out dir `D:\xiaomi13\fuxi_ios\work\loader-autolaunch\build`.
  prepare-kernel-variant-loader.py links those objects + gpu-fw.o + payload.o with ld.lld (loader.ld) -> Image -> mkbootimg v4.
- Planned design (not written yet): in probe.c, a flag set on any vendor (type 0xc0/0x40) request; in probe_main's
  loop, if no vendor request within ~20 s: core_probe(), wait EP0_SETUP then MON_DELEGATE=2, wait MON_ACTIVE,
  th_idle_mode=2, touch_power(9), MON_DEADLINE = far (e.g. +7 days; a host can still set 0x54), MON_LAUNCH=1.
  With the PC monitor attached nothing changes (it sends vendor requests within seconds).
  Then: rebuild objects, relink the cool user loader (fuxigpulevel=24, fuxiperfidle=1000, full GMEM, inline), test
  with `fastboot boot` and NO monitor (no partition write), back up recovery_a, dd the boot.img into recovery_a,
  readback; KSU module: action.sh = map + verify extents against the loader's embedded mapping (port of
  verify-native-map.py checks) + unmap + `reboot recovery`. Return to Android: iOS restart or long-press power.
- Also pending: kernel CI 43577539716 (preset @1 = page 2 Settings) for the usage A/B; lock-screen Collections black.

## 2026-11-14 ~17:05-17:40 option 1 (loader in recovery_a): auto-launch works, recovery route is IMPOSSIBLE on this ABL

- **Auto-launch loader works.** `shim/fuxi-probe/probe.c` (ios-fuxi working tree, uncommitted) gained a block compiled only
  with `-DFUXI_AUTO_LAUNCH_S=<5..60>` (FUXI_XNU_LOADER builds): any vendor-class USB request marks a host; with none for
  N s after usb_start, probe_main does the PC monitor's pre-handoff steps itself: core_probe (CPU_ON cpu@100),
  MON_DELEGATE=1 (USB moves to cpu@100 at EP0_SETUP), th_idle_mode=2, touch_power(9), MON_DEADLINE=0 (no reset
  deadline; 0x54 from a host still sets one), MON_LAUNCH=1. A host request to 0x55 is refused while it runs. Status text
  in monitor_error (read with 0x48, `auto-status.py`). Without the macro probe.o is section-identical to the deployed
  one (checked). NDK clang 17 flags as fuxi-loader/build.sh minus -Wno-error=unterminated-string-initialization.
- Objects: `D:\xiaomi13\fuxi_ios\work\loader-autolaunch\build\probe-auto.o` (sha256 82e02728...7e2). Relink tool
  `prepare-probe-variant-loader.py <out> <src loader> <probe.o>` (same payload.o/gpu-fw.o; differences before probe code
  are only BL/ADR/ADRP/ADD fixups); pre-boot check `verify-probe-variant-payload.py`. Loader
  `native-auto-cool-loader` = native-cool-user-loader + probe-auto.o (boot.img `0cc0461e...765a`, kernel-profpc-artifact).
- Test `native-layout0024-autolaunch-boot-1` (17:10, fastboot boot, NO monitor): USB device up 0.5 s after `fastboot
  boot`; at +40 s `auto-status.py`: delegate 2, active 1, launch 1, stage 8, deadline 0, note `auto: launched, touch power
  00000006`. Monitor attached afterwards (it sees stage 8 and skips the handoff): SpringBoard exec'd, touch sensing up
  (a real contact logged). Back to Android via the monitor's 8 min deadline.
- PMK8550 PON (read under Android, regmap 0-00): pon_pbs 0x840-0x843 = 0f 07 07 80 (KPDPWR S1 10256 ms + S2 2000 ms,
  hard reset, enabled), 0x848-0x84b = 0c 07 01 80 (KPDPWR+RESIN 4480 + 2000 ms, warm reset, enabled). So holding Power
  ~12 s or Power+Vol-down ~6.5 s resets in hardware, whatever the AP runs. Not yet tried under iOS.
- Android 17 init writes BCB `boot-recovery` on `reboot recovery` only if the misc command field is empty (string in
  /system/bin/init). misc held `bootonce-bootloader` (left by our `adb reboot bootloader`) and this ABL booted Android
  with it present (ignored). Backup: `D:\xiaomi13\fuxi_Backup\android-partitions-20261114\misc.img`.
- **Recovery route fails, twice**: loader written to recovery_a (readback OK), KSU action -> `reboot recovery` -> ABL
  ends in fastboot with `slot-unbootable:a:yes` (retry 6); no USB 1209 device ever appeared. Second try with a 50-byte
  dummy ramdisk (`native-auto-cool-rd-loader`), `fastboot flash recovery_a` + `fastboot reboot recovery`: same result.
  Reason: TWRP's recovery_a has **kernel_size 0** (76 MB ramdisk only), i.e. this ABL boots recovery with the **boot_a
  kernel** plus the recovery ramdisk (BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE style); a kernel in recovery_a is never
  run. Confirmed when TWRP came back: it booted with the Android kernel (kernelsu.ko in its ramdisk).
- Recovery each time: `fastboot set_active a` (clears unbootable, retry 7). Restored TWRP with `fastboot flash recovery_a
  D:\xiaomi13\fuxi_Backup\android-partitions-20261107\recovery_a.img` (sha 7d589d3c... verified on the device). The next
  `fastboot reboot` landed in TWRP (PON recovery reason still pending after the failed path); `adb reboot` from TWRP ->
  Android, slot a marked successful again (`bootctl is-slot-marked-successful 0` rc 0). misc command field now all zero.
- KSU module `work\ksu-ios-entry\fuxi_ios_boot` (action: slot, FIEMAP vs loader ADT, pin, recovery_a sha, misc marker,
  `reboot recovery`; post-fs-data clears the marker) installed then **uninstalled** (removed on next reboot).
- Remaining PC-free routes all need boot_b + slot switching (ABL went to fastboot instead of falling back to slot b when
  a was unbootable, so a failed slot-b boot could strand the phone in fastboot without a PC) or kexec (GKI). Not
  pursued; asked the user. With a PC, the auto-launch loader alone (`fastboot boot`, no monitor) is enough.
- Backups moved to `D:\xiaomi13\fuxi_Backup` (README.md index): android-partitions-20261107 (was D:\xiaomi13\fuxi_ios\backup),
  ios-root-images (was D:\xiaomi13\fuxi_ios\work\fuxi-images), ios27-root-recovered-20261108 (was D:\xiaomi13\fuxi_ios\candidates\recovered-root27).
- User asked to rename D:\xiaomi13\fuxi_ios -> D:\fuxi_ios. Script `D:\fuxi_rename\rename-13-to-fuxi_ios.py` (run from D:\ with no
  session/terminal open in D:\xiaomi13\fuxi_ios; rewrites hardcoded paths in 239 text files, dry run clean). Claude memory already
  copied to `%USERPROFILE%\.claude\projects\D--fuxi-ios\memory` with paths rewritten.

## 2026-11-14 ~18:55-19:30 slot-b dual boot: chain image in boot_b WORKS (standalone iOS from the phone)

- Folders: workspace is now `D:\xiaomi13\fuxi_ios` (was D:\13, then D:\fuxi_ios); backups `D:\xiaomi13\fuxi_Backup`.
- Before writing: boot/init_boot/vendor_boot/dtbo/recovery of slots a and b were identical and equal to
  `fuxi_Backup\android-partitions-20261107` (restore point for boot_b: `boot_b.img`, stock); vbmeta_a/b added to
  `android-partitions-20261114`. recovery_a/_b = TWRP 3.7.1 (kernel_size 0, 76 MB ramdisk).
- ABL facts measured: normal and recovery boots both use the boot partition's kernel; kernel load base 0xA8000000;
  initrd ends just below 0xB8000000 (normal 0xB6013000-0xB7FFEF44 = 33 MB; TWRP recovery 0xB1787000- = 107 MB); FDT
  0xB5E12000 (815 KB) right below the initrd. The bootconfig ABL appends to the initrd carries
  `androidboot.force_normal_boot = "0"` on a normal boot and NOT in recovery mode; cmdline differs only in
  `debug_ext.initrd_size`. ABL accepted a 64 MB kernel Image.
- Chain stage (ios-fuxi working tree, uncommitted): `shim/fuxi-loader/chain.c` + `#if FUXI_CHAIN` block in start.S
  (right after BSS clear, before anything touches hardware) + `loader-chain.ld` (Android Image on the next 2 MiB
  boundary after the XNU payload, `_image_end` covers its image_size). chain_select(fdt): /chosen initrd range ->
  bootconfig trailer (magic, size, checksum) -> force_normal_boot present = normal -> XNU loader; absent = recovery ->
  branch in place to the embedded Linux Image (x0 = fdt, x1-x3 = 0, MMU/caches untouched). No bootconfig: initrd
  > 64 MiB = recovery. Bad FDT / no embedded kernel: XNU. Normal builds without FUXI_CHAIN: start.o identical.
  On-phone unit test `work\boot-chain\chain-test.c` (real /sys/firmware/fdt, patched initrd props): 6/6 cases OK.
- Build `work\boot-chain\build-chain.py <name> [--force-linux] [--auto-launch-s N]`: payload of
  native-auto-cool-loader, probe with FUXI_AUTO_LAUNCH_S=5, Android Image from boot_a backup (47.4 MB raw, image_size
  0x2de0000, flags 0xa), header v4, os_version 0 like boot_a. `chain-v1` boot.img 9924fe1c...3f06 (66.3 MB):
  Android at 0x1200000 (runs at 0xA9200000), image end 0x3FE0000.
- Tests: (1) `fastboot boot chain-forcelinux` -> Android, KSU root, /proc/iomem Kernel code a9210000 (normal boot
  a8010000). (2) `fastboot boot chain-v1` -> normal detected -> auto-launch at 5 s -> stage 8, SpringBoard, touch
  sensing (`native-layout0024-chain-v1-boot-1`). (3) boot_b written (readback OK, boot_a untouched), then
  `bootctl set-active-boot-slot 1` + `adb reboot recovery` -> TWRP on slot b (`_b`, kernel at a9210000).
  (4) in TWRP: `/system/bin/bootctl set-active-boot-slot 0` + reboot -> Android slot a, marked successful.
  (5) KSU module v2 action (`work\ksu-ios-entry`, zip `fuxi_ios_boot-chain-v1.zip`, installed): checks slot a,
  FIEMAP == expected, pin, boot_b == chain-v1, recovery_b == TWRP sha, then set slot b + reboot -> **user saw iOS**.
- In a slot-b NORMAL boot the probe's USB never enumerates (ABL only brings USB up in fastboot mode), so the PC sees
  nothing (no monitor, no logs) even though iOS runs. Earlier guess that the loader was rebooting was wrong.
- Return used this time: user Power+Vol-down -> fastboot; `fastboot set_active a` + reboot -> Android. Slot state
  seen in fastboot after one iOS boot: current b, b retry 6, a retry 6 successful. Each iOS boot of slot b uses one
  retry; the KSU action resets b to 7. What ABL does when b's retries run out is still untested.
- Still to verify with the user: Power+Vol-up from iOS -> TWRP (slot b) -> Reboot -> Slot A (ABL key path; the same
  recovery-mode path as `adb reboot recovery`, which passed). Optional: USB in slot-b normal boots, a boot menu.
- 19:3x user verified the whole loop by hand: KSU action -> iOS (slot b); in iOS Power+Vol-up -> TWRP (slot b, via
  the chain stage) -> Reboot -> Slot A -> Android. Afterwards: slot a active and marked successful, b bootable, misc
  command field empty. The dual boot is usable without a PC.

## 2026-11-14 ~19:45-20:45 draw census (kernel-draws-artifact) and the real limit: CA's frame budget vs the vsync phase

- Kernel CI 43591788906 (harness f56f8a5 `kernel-ibprof-draws.patch`: per-job totals with unique draws, submitter pid,
  jobs per pid). `draws-census.py <evidence>`. Loaders `native-draws-{drag,drag90,settings90}-loader` (spin base +
  pace args, fuxiearly=16, sweetprof=40/90, fuxitapcmd=@2 / @1).
- Drag (@2, slow home drag): boot-1 88.2 fps shown / 86.3 Hz, boot-2 (sweetprof=90) 86.9 / 84.9 Hz; 31-32 unique
  draws per job; display thread on CPU only 19 % = 2.2-2.3 ms/frame (~70-75 us per draw incl. everything); gpu_ms 1.4.
  Neither CPU nor GPU limits it. **The @2 drag is speed-capped**: its swipes move 720 px in 8 s = 90 px/s, and the home
  page follows the finger at pixel granularity, so at most ~90 distinct frames/s exist. All "drag Hz" numbers (85-102)
  were bounded by the script, not the stack. Use @1 (Settings flings) or a fast drag for 120 Hz claims.
- Settings (@1: page 2, tap Settings, 4 flings of 1200 px in 300 ms, home, page back; 20 loops): boot
  `native-layout0024-draws-settings-boot-1` 77.2 fps shown / 78.6 Hz, flips 71 % single, gpu_ms 3.66; 90 unique draws
  per job (p90 136); display thread 51 % on CPU = 6.5 ms/frame (72 us per draw). Phases differ a lot (windows 50-93 Hz).
- One-shot frame log of that boot (128 swaps during Settings scrolling): swap interval p50 9.4 ms (~105 Hz); all 128
  shown, none dropped/replaced; 19 two-TE gaps, 17 of them "copy/start late": swap 3.7-5.1 ms after the previous START,
  then GPU client job ~3.85 ms + blit 0.6 ms -> copy done 8.6-8.9 ms after that START, past the next TE (8.27 ms).
- SwapWait is NOT what holds CA back: whole boot `intervals_2plus=3129, busy_before_2plus=33` (fuxiearly 1/2 would not
  help). CA starts each frame at its heartbeat and needs CPU + GPU + blit < budget.
- **The heartbeat phase**: `SWEET FB: vsync te-locked=600 phase_us 7872/7872 period_us 8273` - CLCD notifies at
  estimated TE + `fuxivsynctoff` (default 7800 us, chosen for 60 Hz transfer timing), i.e. only ~0.4 ms before the next
  TE at 120 Hz. A frame that should show at TE k+2 therefore has ~8.7 ms from heartbeat to deadline, and our frames
  take 8.5-9 ms: every fluctuation across that line costs a TE. `fuxivsynctoff=6000` gives ~10.5 ms (boot-arg only).
- Flat on-CPU profile of the display thread re-read (profedge boot, exact addresses, 22426 live samples): m2v entry
  points (nmb_render_draw_indexed 27 %, nmb_render_draw 4 %, nmb_encoder_end 6 %, commit 3 %, encoder_new 2.5 %) ~45 %,
  Turnip inside them ~6 % (tu_CmdBindPipeline 2.7 %: one big struct copy, memmove 428 edges = 1.9 %); self time:
  memmove 7.2 %, malloc family 5.9 % + its os_unfair_lock ~2.6 %, mach_msg traps 3.6 % (io_connect_method: SwapWait
  214, darwin_queue_submit 216, SwapEnd 103 edges; IORegistryEntrySearchCFProperty 138 per frame), Rust std Mutex
  (pthread) 2 %. PROFEDGE overflowed (6502 dropped), so malloc callers are not attributable from it.
- boot-exp.ps1 fix: the battery temperature read failed when adb returned several lines (`System.Object[]` cast);
  now takes the first number (`grep -m1`), default 999.
- A/B started: `native-draws-settings-vt6-loader` = settings90 + `fuxivsynctoff=6000` (args 577), boot
  `native-layout0024-settings-vt6-boot-1`.
- **A/B result `native-layout0024-settings-vt6-boot-1`** (fuxivsynctoff=6000; log `vsync TE requested offset_us=6000`,
  windows `te-locked=600 phase_us 6080/6080 period_us 8271-8275`): the one-shot frame log (128 swaps, again during the
  Settings flings, loop 2) went from swap p50 9449 us / 19 two-TE gaps to **p50 8257 us (one TE, ~119 Hz) / 5 gaps**
  (4 frames replaced before START). Whole-script numbers did not move: backboardd 74.5 Hz (78.6), flips 72 % single
  (71 %), 600-START windows still 100-170 two-TE gaps each; that boot ran hotter (governor max 72.2 C vs 68.2).
  Reading: the scroll phase is now paced at the panel rate; the other phases (app open/close, home paging) have frames
  whose CPU alone is near or above the budget (90 unique draws/job mean, p90 136 at ~72 us each), so they need cheaper
  draws. 600-START windows mix phases; a per-step metric would need a kernel change (TAPS + scanout counters).
- Frontend (harness codex/astc-layout-backport 9eec0c0, iOS CI 43596634353, ~20 min with LTO):
  `fuxi-bridge-lto-builder.patch` (bridge manifest `lto = "fat"`, `codegen-units = 1`; the bridge had neither) and
  `nativemetal-fuxi-0015-thread-cache.patch` (bridge/FuxiThreadCache.rs = #[global_allocator]; `fuxitcache=1` boot arg
  read once per process via kern.bootargs; per-thread lists of freed blocks 16..512 B in 16-byte classes, 64 per class,
  pthread-key destructor at thread exit; blocks are plain malloc blocks of their class size; logs `FUXI TCACHE: on`).
  `verify-ci-artifact-0025.py`: changed vs 0024 only libnative_metal_bridge.dylib (`ca8d5b60...49f3`) + source tarballs +
  build log; the `fuxitcache=1` literal is folded into immediate compares under LTO (not in the binary as a string).
- **Image 0025** = 0024 + that bridge: share `work\ci\native-layout0025-share`, overlay `work\images-build\
  native-layout0025-overlay.qcow2` over the re-hashed 0024 raw (`f128ca55...1641`), stage-test.sh: INSPECT_SUCCESS, fsck OK
  x3, staged bridge hash = artifact. Export `D:\xiaomi13\fuxi_Backup\ios-root-images\native-layout0025-root.img` sha256
  `9dd1e7a05186de8d5cb0dff1410a59c4e8bf4a859347165d067a7b62011bc0d9`. Deploy script `deploy-layout0025.sh` (map,
  verify-native-map, delta-stage-hash, current sha, writer/supervisor derived from 0024's, readback, cleanup).
- Loaders for the 0025 A/B (kernel-draws-artifact): `native-draws-settings-vt6-loader` (LTO alone vs the 0024 vt6 boot)
  and `native-draws-settings-vt6tc-loader` (+ fuxitcache=1, args 590).
- Turnip note (Mesa 85fda3b, `work\mesa-ref`): tu_CmdBindPipeline does `cmd->state.program = pipeline->program`, a
  struct copy of tu_program_state (14 x ir3_const_state linkage + 14 x 65-byte blake3 strings), the memmove seen in the
  profile (~2 % of the display thread). 13 uses in tu_cmd_buffer.cc; a pointer would remove it. Needs a Mesa rebuild
  lane (the deployed dylib 74670207... is old), not started.
- **0025 deployed 21:15-21:23** (`deploy-layout0025.log`): phone before `633bea5039e7cef87753c0cd88317393b568ccb42b9b3fc10e4d8d9360bba980`
  (NOT backed up), 489 differing blocks (1956 MiB), STAGE_VERIFIED, writer `bd753ff2...0236` / supervisor `ba5ca51b...504b`,
  status 0 (dd 22.4 s), READBACK_MATCH `9dd1e7a0...c0d9`. **Phone root: 0025.**
- **User (21:2x): "本次桌面布局被打回默認"**, photo: page 1 = Photos, App Store, Settings (row 2 col 1), Photos widget; dock
  Phone/Safari/Messages. Cause: the image was built on the host's clean 0024 raw, so the delta wrote back every block that
  differed from it - all changes made in iOS since 12:25 (the user's own home layout: Settings on page 2 row 1 col 1,
  etc.) were reverted. Not recoverable (no copy of the phone state). Memory `deploy-preserve-phone-data`. New tool
  `delta-pull-hash.py` (phone block hashes -> pull only blocks that differ from the host base -> raw copy of the base with
  them written in place, every block re-checked against the phone's hashes, `.sha256`): the next deploy stages on THAT
  (overlay + guest staging + `qemu-img commit`, tested on a small image) so user data survives. Windows qemu-io
  `write -s` corrupts the source data from byte 488 (text-mode read?): not used.
- The first 0025 A/B pair ran the @1 page-2 script against the default layout: Spotlight instead of Settings
  (`exec Preferences` 0) -> invalid; second boot cancelled. 0025 itself was stable there (no backboardd/SpringBoard crash,
  no DeviceLost, no NMB VK ERROR). That load (home + Spotlight) is another heavy case: 54 Hz, gpu_ms 5.7, per job 82 unique
  draws, 35 render passes (p90 74), 114 blits (p90 431), display thread 8.85 ms/frame.
- Kernel `kernel-taps-page1.patch` (harness 0b71826, CI 43600008393, `kernel-page1-artifact`): `fuxitapcmd=@3` = the old
  page-1 Settings session (tap 148,570). Loaders `native-page1-spin-loader` (kernel variant of draws-spin),
  `native-p1-vt6-loader`, `native-p1-vt6tc-loader` (+fuxitcache=1).
- `native-layout0025-p1-vt6-boot-1` (LTO, tcache off, Settings opened): backboardd 79.6 Hz, flips 82 % single (0024 page-2
  runs 71-72 %), gpu_ms 4.33; display thread 6.04 ms/frame, 96.7 unique draws/job = 62.5 us per draw (0024: ~72). Layouts
  differ, so LTO vs 0024 is indicative only.
- `native-layout0025-p1-vt6tc-boot-1` (same, + fuxitcache=1; log `|backboardd| FUXI TCACHE: on`; Settings opened; no
  backboardd/SpringBoard crash, no DeviceLost): backboardd 81.7 Hz (79.6), flips 84 % single (82 %), gpu_ms 4.35, display
  thread 5.73 ms/frame (6.04), 99.2 unique draws/job = 57.7 us per draw (62.5); ran hotter (max 73.8 C vs 71.4) with
  less boost (158.6 s vs 175.0). The thread cache is a small, real gain (~5 % per frame); kept on in the user loader.
  0024 -> 0025+tcache per draw ~72 -> 57.7 us (layout caveat). Heavy frames still: Settings p90 well over budget, and the
  home/Spotlight load has ~35 render passes per frame; next target = per-render-pass cost in m2v (each encoder opens its
  own secondary VkCommandBuffer and initialises a very large RenderCommandEncoder by value) and a fresh exact profile.
- `delta-pull-hash.py` gained `--keep-delta <new dir>` on disk (keeps pull.bin + block lists + README as a compact
  "base + delta" backup of the phone state); consistent, compiles; not written by this session's earlier edit.
- User loader for 0025: `native-vt6tc-user-loader` (kernel-draws-artifact; cool idle args + fuxivsynctoff=6000 +
  fuxitcache=1; sweetnull without fuxi-ca-smoke, no keepsyms; args 592; boot.img `98fb8642...736c`). Not booted yet.
  The slot-b chain (standalone boot) still carries native-auto-cool-loader's args; rebuilding it means rewriting boot_b.

## 2026-11-14 ~22:00 hybrid lane ("mixed driver") P0: plan, draw census patch, Turnip rebuild gate

- Plan (zh-TW): `D:\xiaomi13\fuxi_ios\work\hybrid-driver\PLAN.zh-TW.md`. Lane = L1 recorder inside metal2vulkan-runtime
  (structural eligibility per pipeline, dirty-tracked bind recipes, skip settle/plan/heap-sets; anything else falls back
  to the existing path) then L2 = a Turnip-private entry point that takes packed draws and writes PM4 itself (inside
  Turnip so GMEM per-tile replay and its dirty tracking stay correct). Boot arg `fuxihyb`: absent/0 = today's
  behaviour, 1 = census, 2 = lane (planned).
- Why: 0024 Settings boot, ~72 us per draw all-in, 90 draws/job (p90 136), display thread 6.5 ms/frame; Turnip only
  ~6 % of the thread. `render.rs` draw/draw_resident run the full Metal emulation per draw (tess/mesh checks,
  extended shadows, reroute (+ a Vec collect), restart, pass-start copies, capture, reformats, settle_draw under the
  live lock, instantiate_draw_into, heap sets, texture table, index bounds/rewrite passes, coverage) and
  `pipeline.function_tables()` returns a fresh Vec per draw.
- **metal2vulkan patch 0010** (`m2vb` commit "0010 draw census"; harness `patches/metal2vulkan-fuxi-0010-draw-census.patch`):
  new `fuxi_census.rs` + hooks in the 34 render setters, `draw` and `draw_resident`. `fuxihyb>=1` (kern.bootargs,
  once per process). Per thread: pipeline structural traits (fetch/input attachments, tess, mesh, storage
  attachments, imageblock, shadow stencil, amplification, sample shading, shader_tests, tile memory, function tables,
  viewport index, vertex writes, discard, msaa), runtime steps (reformat, restart, rewrite, bounds, capture, skipped),
  bind-call kinds since the previous draw (mask), phase times (entry..settle, settle incl. lock, instantiate, rest of
  draw_resident, post), time between draws (<0.5 ms), same pipeline/variant as previous, distinct pipelines/variants,
  top 12 pipelines. Every 10 s: 4 lines `FUXI HYB T<n> pid=<p> win_s=...|traits|sets|top` to stderr (lands in boot.log
  as `|backboardd| FUXI HYB ...`). Local `cargo check -p metal2vulkan-runtime --ignore-rust-version` (rustc 1.97.1,
  with a never-committed Windows stub for the unix-only `Device::foreign_acquirer`) passes; target dir
  `work\hybrid-driver\target`. Parser: `work\hybrid-driver\tools\hyb-census.py <evidence> [--proc backboardd] [--windows]`
  (tested on a synthetic log only).
- Harness branch `codex/hybrid-lane` (6822727, from astc-layout-backport 9eec0c0): native-metal-check.yml copies 0010;
  new `turnip-rebuild.yml` (push-triggered on this branch) rebuilds Turnip from the deployed dylib's exact inputs —
  its build log (`native-share\mesa\logs\build.log`) says ios-fuxi `f44bf5693da1...`, Xcode 16.2, MESA 85fda3b,
  UNIXNU fb6aad6a, 9 UniXNU + 6 mesa-fuxi patches (incl. mesa-fuxi-bo-cache, absent from the 979e tree) — and
  compares with `7467020727fcf6d7f98a047bbeaeef4ffe384775890ead544ffff0a18e637c51`. Later `patches/mesa-hyb-*.patch`
  get appended to build-fuxi-turnip.py's hard-coded patch list by the workflow. Runs: frontend 43602677972
  (dispatch), Turnip 43602667374 (push).
- Loaders (host only): `native-hyb1-p1-loader` = native-p1-vt6tc-loader (kernel-page1-artifact, @3 Settings from
  page 1, vt6000, tcache) + fuxihyb=1, args 600, boot 701f70f9...; `native-hyb1-user-loader` = native-vt6tc-user-loader
  + fuxihyb=1, args 602. (`native-draws-settings-vt6tc-loader` uses @1 = page-2 layout, invalid since 0025 reset the
  home screen.)
- 0025 A/B left unfinished by the previous session: p1-vt6 boot ran (Android did not come back in time), p1-vt6tc
  never ran, neither analysed.
- `delta-pull-hash.py` gained `--keep-delta DIR` (moves pull.bin + diff/phone block lists + README with hashes there:
  base + delta = compact backup of the phone state). First real run started: base 0025 raw, out
  `work\images-build\native-phone-20261114-root.img`, delta kept in `D:\xiaomi13\fuxi_Backup\ios-root-phone-20261114`
  (driver `work\hybrid-driver\pull-phone-20261114.sh`, log alongside).
- **Turnip rebuild gate result**: run 43602667374 -> `04ae43c0fd80...`, run 43603522908 (same recipe, same inputs) ->
  `c97ac49aadf5...`, deployed `74670207...`: all differ, all 17878576 bytes. Same AppleClang 16.0.0.16000026, Xcode 16.2
  16C5032a, meson 1.12.1, mako 1.4.3; printable strings identical (no embedded paths); run 1 vs deployed: `__text`
  456 bytes shorter, every later `__TEXT` section shifted by 0x1c8 (6.1 MB of byte differences). So the recipe is NOT
  deterministic (suspect Python hash ordering in Mesa's generators); the deployed dylib is just one sample of it.
  For L2: pin PYTHONHASHSEED (and check two runs match) before trusting byte comparisons, and boot a "rebuilt Turnip,
  no lane code" image once before any lane patch. Artifact of run 1 kept in `work\hybrid-driver\turnip-rebuild-gate`.
- Pull attempt 1 (22:04-22:06): phone hashes OK, **299 blocks (1196 MiB) differ from the 0025 base**, then adb vanished
  before packing (another session "接續" fastboot-booted `native-vt6tc-user-loader` at 22:08 via WMI: run-monitor
  `-Seconds 1800 -Reset 30`, out `native-layout0025-vt6tc-user-boot-1`). Nothing written; leftover
  `/data/local/tmp/codex-phone1114-blockhash.sh` on the phone; the pull script now cleans it and retries.
- **Pull attempt 2 OK** (22:21-22:29, after the other session's monitor ended at 22:18; guarded by
  `work\hybrid-driver\wait-then-pull.sh`, which waits for no run-monitor/boot-exp/fuxi-xnu-monitor process and 3x20 s
  of boot_completed): 316 blocks (1264 MiB) differ from the 0025 base, all verified against the phone's hashes;
  phone-state raw `work\images-build\native-phone-20261114-root.img` sha256 `10410d0e79c551ad...7c00a`; delta kept in
  `D:\xiaomi13\fuxi_Backup\ios-root-phone-20261114` (pull.bin 1.3 GB, sha256 fa0996ba...; README index updated);
  mapper unmapped. Note `reset-soon.py` needs the user site-packages (pyusb): run it without `python -I`; it fails
  with Access denied while another monitor holds the probe.
- **Frontend CI 43602677972 OK**: artifact `work\ci\native-layout0026-artifact`, vs 0025 share only
  libnative_metal_bridge.dylib (`0cbfd1761eef3e7f0f72ee088e34cc77d1ea692585fdf8ae49183e8fa59a0ed9`), both source tarballs and
  build.log changed; nativemetal-source.tar.gz carries fuxi_census.rs and patch 0010 (metal2vulkan-source.tar.gz is the
  old FuxiMetal translator's tree and does not). Image build: `work\hybrid-driver\build-0026.sh` (share = 0025 share
  with native/ replaced; overlay on the phone-state raw; guest-run stage-test.sh; qemu-img commit; rename to
  native-layout0026-root.img).
- **0026 staging attempt 4 failed (22:31), cause found (session "001 作業續", 22:35)**: the phone-state raw is NOT a
  clean host image: the iOS root was reset, so its HFS+ journal still holds committed transactions (probe dry run on
  the raw: little-endian, start 5180928 end 724992, **17 block lists, 347 blocks, 2609152 bytes to replay**), and the
  0025 `stage-test.sh` requires `JOURNAL_EMPTY` before touching the volume (fails silently via `grep -q`). Fix follows
  upstream `tools/root-edit-linux.sh`: new `work\hybrid-driver\stage-test-phonebase.sh` = replay `--apply` on /dev/vda
  (the qcow2 overlay; the raw base stays untouched) -> require JOURNAL_EMPTY -> fsck that tolerates only "Orphaned open
  unlinked file" (iOS removes them at its next rw mount) -> mark-unmounted -> stage as before -> fsck -> mark-journaled
  -> JOURNAL_EMPTY -> fsck. `build-0026.sh` copies it over the share's stage-test.sh. The share's
  `hfs-journal-block-probe.py` = upstream `hfs-journal-replay.py` + BLKGETSIZE64 for block devices. On Windows the
  probe needs an `os.pread` shim (seek+read) to dry-run a raw file. Any future build on a pulled phone image needs this.
- **0026 built (22:35-22:39)**: `work\images-build\native-layout0026-root.img` sha256
  `c5cac89a1cd70c291b1f0494245a1de4563b73f46d496b8b0ff86dd8622b678b` = phone state 10410d0e... + journal replay (17
  lists / 347 blocks) + census bridge `0cbfd176...`. Guest: JOURNAL_REPLAYED, fsck before/after/final all "appears to
  be OK" (no orphans at all), INSPECT_SUCCESS. Host re-check of the raw: HX, lastMountedVersion HFSJ, attr 0x80002100
  (unmounted + journaled), JOURNAL_EMPTY. The phone-state raw was renamed into the 0026 raw (no separate copy left;
  rebuild it from `fuxi_Backup\ios-root-images\native-layout0025-root.img` + `fuxi_Backup\ios-root-phone-20261114`).
  Deploy driver `work\hybrid-driver\deploy-layout0026.sh` reviewed against deploy-layout0025.sh (only adds the
  "phone mapper sha must still be 10410d0e..." guard, exit 6). Not written to the phone yet.
- **0026 deployed (22:42-22:51)**: phone mapper was still 10410d0e... (guard passed); delta 33 blocks / 132 MiB;
  stage verified c5cac89a...; write status 0 in 64 s; READBACK_MATCH c5cac89a1cd70c291b1f0494245a1de4563b73f46d496b8b0ff86dd8622b678b.
  Phone root is now 0026. Log `work\hybrid-driver\deploy-layout0026.log`.
- **Loader verify fix**: `verify-args-on-loader-payload.py` rejected `native-hyb1-user-loader` (its reference was
  native-vt6tc-user-loader and swaps.txt only had `+fuxihyb=1`, so the inherited `fuxiperf=2` counted as out of
  scope). Re-based it on `native-draws-spin-loader` with the parent's full swaps + `+fuxihyb=1` (old files kept as
  `*.vs-vt6tc`); payload/boot.img untouched; verify OK.
- **P1 census boot `native-layout0026-hyb1-user-boot-1`** (22:53:10, user loader + fuxihyb=1, user operating by hand:
  Settings, apps, Safari, App Store, control centre, widgets, lock): 0 panic / VK ERROR / DeviceLost, host tsens max
  67.3 C, boot.log 23885 lines sha 0cbcd988.... Census only in backboardd (pid 56). Summary
  `work\hybrid-driver\census-0026-user-boot-1.txt`. backboardd T1 (render thread, 3.53 M draws in 671 s; busy windows
  40k-133k draws / 10 s): **eligible 79.0 %**; the only excluding trait is framebuffer fetch (21 %); **no runtime steps
  at all** (no reformat/restart/rewrite/bounds/capture/skip). us/draw inside m2v **37.4 = pre 2.5 + settle 5.4 + inst 3.7
  + rest 24.8 + post 1.0**, between draws 29.7 (37.4 + 29.7 = 67 us, consistent with the 0024 estimate of ~72). Binds
  before a draw: vbuf 89 %, pipe 75 %, ftex 44 %, fbuf 31 %, view 30 %, fsamp 20 %; top sets pipe+vbuf+ftex 16 %, pipe+vbuf
  15 %, vbuf 10 %. Same pipeline as previous draw only 19 % (variants = pipelines 1:1); 5-127 distinct pipelines per
  10 s, top5 48 %, top12 67 % of draws -> the recipe cache must hold ~128+ pipelines. T3 (75 k draws, 92 % eligible, 54.5
  us/draw) and T2 (4 k) are minor. Combined with the 0024 PROFINC split (draw_resident 21.2 % of the thread, of which
  Recording::record 4.5 %, the rest mostly inline emulation bookkeeping), the irreducible part of "rest" is roughly a
  fifth; my estimate for L1 on eligible draws: ~37 -> ~10 us, i.e. ~21 us saved per average draw, ~2 ms per frame at
  ~100 draws (display thread ~6.5-7.2 ms/frame) -- an estimate, not measured.
- **Log splitting**: long census lines (sets/top, up to ~650 chars) reach boot.log split into an unprefixed kernel
  fragment (<= 251 chars) + `|backboardd| ` continuations at arbitrary byte offsets, sometimes two pieces on one line
  and a few bytes lost. `hyb-census.py` now reassembles per process (splits glued pieces at `FUXI `, stricter mask
  regex) and prints a data-quality line: T1 sets 51/54 windows (masks cover 84 %, the rest is the truncated tail list),
  top only 21/54; T2/T3 ~100 %. Future census lines should stay < 250 chars.
- **Reset did not happen at +13 min**: run-monitor renews the phone's reset deadline every 600 s, so with -Seconds 720
  -Reset 13 the 600 s renewal moved it to ~23:16. After boot-exp returns without Android, run
  `python reset-soon.py 1` (plain python, not -I): sent 23:08:52, Android boot_completed 23:10:39.

## 2026-11-14 ~23:15 hardware lane ("000"): panel brightness `fuxibacklight=4` (offline, phone untouched)

- Why modes 1-3 never dimmed: CoreBrightness takes the built-in backlight only with `backlight-control = true` AND
  `IOObjectConformsTo(service, "AppleARMBacklight")` (findBacklight); FuxiBacklight conforms to neither class name. This
  kernel has no Apple AppleARMBacklight class (no kexts in the iOS 16 lane; AppleCLCD/AppleKeyStore are reimplemented the
  same way), so mode 4 publishes a service of that class itself. agy's `0117` draft (own thread writing DSI0) not used.
- Kernel source worktree `work\backlight-dsi\ksrc` (branch `backlight-dsi` of kdraws-src, from 6fc377f = 979e + harness
  patches through taps-page1), commit eafc778:
  - `xnu/fuxi-backlight/FuxiDsiBacklight.cpp` + `fuxi_dsi_backlight_logic.h` (new, in 0109 via sync-fuxi-backlight.py,
    build line `iokit/Kernel/arm/FuxiDsiBacklight.cpp`): `AppleARMBacklight : IOService` under AppleCLCD, backlight-control,
    IODisplayParameters brightness 0..2047 (initial 1023), setProperties accepts brightness (number / {value} / inside
    IODisplayParameters) + FadePeriod (16.16 s, cap 5 s), logs the first 64 request shapes, never waits (atomic request +
    1 us timer), fades on its own work loop in 16 ms steps, first request applied at once, levels < 8 sent as 8. No MMIO.
    `fuxi_backlight_publish` dispatches mode 4 before the mode 1-3 gate (FuxiBacklight.cpp otherwise unchanged).
  - `xnu/fuxi-adreno/fuxi_gpu_scanout.inc` (0098 via sync-fuxi-metal.py): `fuxi_gpu_scanout_backlight(level)` stores the
    newest level; `fg_bl_flush` (under FG_GUARD) sends one DCS 0x51 long write (words c0390003, ff|lo|hi|51) with the
    panel120 register sequence (TRIG low nibble 4, bits 12/16 clear; INT_CTRL done W1C; FIFO reset; TPG 0x30006; DMA_CTRL
    embedded/HS; LENGTH 8; SW trigger 0x090) and restores everything as after panel120. Only when owned, panel120 attempted,
    AR off, no mid-period START, CTL START clear, write line idle, DSI status idle (50 us bound), ctrl&7==5, TPG idle, no
    error. Called in fg_scanout_publish right before the START write, and from a thread call (static screen, 4 ms x 12).
    Uncertain command (error or no done in 2 ms): restore, halt brightness for the boot; still busy 20 ms later ->
    fg_scanout_stop. Needs fuxigpuscanout=1 + fuxipanel120=1 (DSI0 window mapped only then; current user args have both).
  - docs-lanes/backlight.md: new section "Panel brightness (fuxibacklight=4)".
  - Windows note: upstream sync tools fail here (git apply stdin and Path.write_text get CRLF); run them through
    `work/backlight-dsi/tools/run_lf.py` (forces LF; `python -X utf8 run_lf.py tools/sync-....py`) or the patches are rewritten wholesale.
- Harness branch `codex/backlight-dsi` (from codex/kernel-bo-slots 0b71826, worktree `work\backlight-dsi\harness`):
  795e97a + 77940d8 = `patches/kernel-backlight-dsi.patch` (git diff 6fc377f..eafc778, applies on kdraws-src 6fc377f),
  `tests/test-backlight-dsi.py` (structure + the extracted DCS block against a simulated DSI controller: FIFO words, DMA
  config at trigger, 15 deferral cases with no writes, restore after good/stale/error/no-completion, halt, newer level
  during a send, every MMIO write failure -> stop, timer retry/give-up; plus logic header arithmetic),
  `.github/workflows/kernel-backlight-dsi.yml` (push-triggered on this branch, applies after taps-page1, artifact
  `fuxi-kernel-backlight-dsi`). Test passed in the Alpine guest (gcc, UBSan trap; `work\backlight-dsi\guest-run-cc.py`,
  log guest-run-3.log); macOS CI runs it with ASan/UBSan. Kernel CI run 43608992529 started 15:13Z.
- Not done: kernel CI result, loader (plan: prepare-kernel-variant-loader.py native-bl4k-user-loader from
  native-vt6tc-user-loader + kernel-backlight-dsi-artifact, then prepare-args-on-loader.py +fuxibacklight=4), any boot.
  Open on device: iOS 16 request shape/range, whether conformance changes backboardd startup (mode 1 was +5 s), DCS time
  before START.

## 2026-11-14 ~23:15 hybrid lane P2 step 1: split "rest" (patch 0011), pull in place

- Why first: the P1 "rest" (24.8 of 37.4 us) spans instance choice, binding tables, uses, shadow checks, vertex
  buffers, fixed state, degrade notices, VkPipeline lookup, descriptor bind and recording; caching blind could hit the
  cheap parts. PLAN section 6b has the P2 steps.
- **metal2vulkan patch 0011** (`m2vb` ea36ec6 "0011 census rest split"; harness 85b2fdd on codex/hybrid-lane,
  `patches/metal2vulkan-fuxi-0011-census-rest.patch` = `git -C m2vb diff 627082b ea36ec6`, same method as 0010
  (regenerated 0010 byte-identical)): nine more marks in draw_resident (Heap after heap sets/texture table/skip check,
  Instance after next_instance + shadow/stand-in/imageblock, Tables after binding tables/command_uses_in/skip, Shadows
  after shadow uses + vertex buffers, Fixed after draw_fixed/bias/viewports/draw_states, Notices after key +
  degrade_* + binds_each, Pipeline after begin_graphics_pipeline/first_use, Bind after bind/push/write_sets, Uses before
  records_executions) and a report line `FUXI HYB T<n> pid=<p> rest n=<draws with every mark> heap= instance= tables=
  shadows= fixed= notices= pipe= bind= uses= rec=` (us per draw). Every report line is now its own write and < ~240
  bytes (sets / masks / two top lines of six), which should stop the boot-log splitting. Behaviour unchanged,
  fuxihyb>=1 only. cargo check (Windows, temporary foreign_acquirer stub appended to texture.rs and removed) passes with
  only the 16 pre-existing dead-code warnings. Frontend CI run 43609675593.
- `hyb-census.py` reads both formats (0010: masks at the end of sets; 0011: masks line, rest line, two top lines) and
  prints `rest split (...)` weighted by n. Tested: 0026 log gives the same T1/T3 numbers; a synthetic 0011 log parses.
- **D: has 23 GB free**, so no second 20 GiB raw: new `Codex-Xiaomi13-GPU-20261109\delta-pull-inplace.py` hashes the
  phone once, diffs it against the fixed backup base (fuxi_Backup 0025) and against the target raw (0026), pulls the
  union, writes the backup delta vs 0025 first (`fuxi_Backup\ios-root-phone-20261114b`, restorable on its own), then
  patches the 0026 raw in place, re-checks every block against the phone and renames it to
  `images-build\native-phone-20261114b-root.img`. Driver `hybrid-driver\pull-phone-20261114b.sh` (refuses while any
  run-monitor/boot-exp/fuxi-xnu-monitor runs). Started 23:22. `build-0027.sh` / `deploy-layout0027.sh <phone sha>`
  derived from the 0026 pair (diffs reviewed: names, base, 0011 check, compare against the 0026 artifact).
- **Kernel CI 43608992529 OK** (9m40s, harness 77940d8): test-backlight-dsi passed on the runner (ASan/UBSan), upstream
  test-fuxi-backlight + sync checks passed, FuxiDsiBacklight.cpp compiled, no new warnings in the added scanout code
  (only the file-wide existing -Wdeclaration-after-statement style). Artifact `fuxi-kernel-backlight-dsi` ZIP sha256
  `6dc40ecd9f1b227fa59d622ab52801ce15c8f813360639772835bfbb4b43d111` = GitHub digest; SHA256SUMS all OK; cmdline.txt equals
  kernel-draws-artifact's; unpacked to `Codex-Xiaomi13-GPU-20261109\kernel-backlight-dsi-artifact` (kernel sha256
  `36ac31931b809928a545102972995727e9fcd43ea39e1e4e5af2f959da479e66`, contains the AppleARMBacklight/DCS strings).
- Loaders (host only, NOT booted): `native-bl4k-user-loader` = prepare-kernel-variant-loader.py from
  native-vt6tc-user-loader (same ADT/args, new kernel); `native-bl4-user-loader` = prepare-args-on-loader.py on it with
  `debug=0xc=>debug=12` (same value, frees one byte: the args field is < 608 and was 592) + `fuxibacklight=4`; args 607,
  boot.img `5539cdd589d57973ef0344234d088d26cf56c3cbe505631362afdd110381fc23`. Host check: ADT identical to the
  vt6tc-user loader, args differ only by those tokens. verify-args-on-loader-payload.py is stale for this chain (asserts no
  `fuxiperf=` although the source loader already has it) and needs the phone mapping; not used. The KSU slot-b chain is
  unchanged (still the old args).
- First boot (when the phone is free and the hybrid-lane session is not using it): look for `FUXI BACKLIGHT DSI:` lines,
  drag the Control Center brightness slider, compare backboardd first-frame time with the vt6tc-user boot.

## 2026-11-14 ~23:40 - 11-15 hybrid lane P2 step 1 result (0027) and step 1b (patch 0012)

- **0027 built and deployed** (23:37-23:48, by the previous "001" session): `images-build\native-layout0027-root.img`
  sha256 `33523765dabdecd58d5bd286d869bbc774343679a4aa86a96d4b102811b77646` = phone state bd13ef7b... (pull 20261114b,
  380 blocks vs 0025 kept in `fuxi_Backup\ios-root-phone-20261114b`) + journal replay + bridge `66fdf295...` (CI
  43609675593, patch 0011). Phone delta 30 blocks; READBACK_MATCH. Logs `hybrid-driver\build-0027.log`,
  `deploy-layout0027.log`.
- **Boot `native-layout0027-hyb1-p1-boot-1`** (23:51, `native-hyb1-p1-loader`, kernel-page1-artifact, @3 Settings
  script, 420 s, -Reset 8): Settings opened (`exec Preferences`), Android back by itself, boot.log 13256 lines sha
  c94864b4..., host tsens max 59.9 C. Report lines are no longer split (0011's one write per line works).
  `hyb-census.py`: backboardd T1 374 s, 2.26 M draws (6041/s), eligible 86.6 % (fetch 13.4 % the only exclusion, no
  runtime steps), **29.6 us/draw = pre 1.9 + settle 3.7 + inst 2.6 + rest 20.5 + post 0.8, between draws 18.3**.
  **Rest split: rec 9.6, bind 3.0, uses 2.2, instance 1.1, tables 1.1, fixed 0.9, pipe 0.8, notices 0.8, shadows 0.6,
  heap 0.4.** Same pipeline as previous draw 11.4 %; 17-73 pipelines per window; top12 79 %. Bind calls before a draw:
  vbuf 100 %, pipe 91 %, ftex 51 %, view 36 %, fbuf 26 %, fsamp 17 %.
  `draws-census.py`: backboardd 84.3 jobs/s, display thread on CPU 49.6 % -> 5.88 ms CPU/frame (0025 p1-vt6tc without
  census: 81.7 Hz, 5.73 ms). 6041 draws/s x 29.6 us = 0.18 s/s, i.e. m2v's draw is ~36 % of the display thread's CPU
  (~72 draws per frame by draws/s / jobs/s; IBPROF's 103.5 unique draws per sampled job counts other jobs too).
- **sweetprof in the same boot (PROFINC/PROFEDGE, kernel-page1 has both)**: bridge slide 0x10a260000 (macho-fit on the
  0x10a27.. group); draw_resident = 0xa3c260..0xa48ccc of the 0027 (patch 0011) bridge, `fuxi_census::mark` is an
  out-of-line call, so its 13 call sites split the function exactly (scratch tools `dr_split.py`, `bridge_top.py`).
  Reliable at pc/lr level only: deeper frames mix stale registers with live stack memory (impossible edges such as
  retain_draw -> drop_glue<DrawBuffers>, a46bc8 -> a dozen different draw() call sites: two callees of draw_resident
  share one frame slot). With that caveat: in the recording, `Recording::record` ~572 samples (~3.7 us/draw), of which
  the `blr` to the device table +0x2d0 (vkCmdDrawIndexed -> Turnip) ~325 (~2 us); retain_draw ~160-210; the 452 at
  draw_resident+0xa968 (after drop_glue<DrawBuffers>) is a stale-frame artifact. Also: the root-bucket denominator is
  cut by the 24-frame stack limit, so the tool's percentages are ~2x too high (root 6864 vs 14874 on-CPU samples).
  Per-encoder costs are large too: ~32 render passes per job, ~3 draws per pass; end_encoding/encoder_new/commit sit next
  to draw_indexed at the top of the bridge's entry points.
- **metal2vulkan patch 0012** (`m2vb` ba0a2de "0012 census pre/settle/rec split"; harness 1a6fe04 on codex/hybrid-lane,
  `patches/metal2vulkan-fuxi-0012-census-split.patch` = `git -C m2vb diff ea36ec6 ba0a2de`, 0011 regenerated byte-
  identical): marks for the entry (ext = extended shadows + reroute, settings = draw_settings, tables = argument samplers /
  function tables / capture / pass-start tables, colors = kept colors + reformats), the settle (refresh =
  refresh_bind_state, fields = other fields + reshape, shape = LiveBind::shape, lookup = settle_shape, fit), the
  recording (prep, setup = install/copies/barriers, record = the Vulkan recording loop, keep = keep_draw/keep_capture,
  notes = degrade/announce/executed, retain = all retains, tail = uses_room + drops + return); events reshaped /
  shapefound / shapemade (counts, not runtime steps: eligibility masks them out). Current-draw marks now one Cell each.
  cargo fmt on m2vb touches many unrelated files (tree not fmt-clean): only the three edited files are committed, and
  render.rs's two unrelated fmt hunks were put back. m2vb commits need `-c user.name=x -c user.email=x@x` (no identity
  configured, earlier commits use x). Frontend CI 43614089135 (a first dispatch at 85b2fdd, 43614043570, was cancelled).
  `hyb-census.py` prints the three new splits and the shape counts (tested on a synthetic 0012 log).
- Next: pull 20261115 (`hybrid-driver\pull-phone-20261115.sh`, in place over the 0027 raw, delta vs 0025 kept in
  `fuxi_Backup\ios-root-phone-20261115`), `build-0028.sh <run>`, `deploy-layout0028.sh <phone sha>`, boot the p1 loader.

## 2026-11-15 ~00:30 hardware lane ("000") step 2: audio DSP + battery manager `fuxiadsp` (offline, phone untouched)

- Previous 000 session (1ed6041d) stopped at its usage limit at 23:47 with the code written but never compiled or wired
  into the build. This session finished it. The phone stays with the hybrid lane (0027 written 23:48); not touched.
- Kernel source worktree `work\adsp-battery\ksrc` (branch `adsp-battery` of kdraws-src from eafc778 = backlight):
  d5c44c3 (patch 0117 + 0106), f1a0846 (0111 regenerated: its sweet_ufs.c hunk moved), 478bf32 (phase 2).
  - `xnu/fuxi-adsp/` -> new patch **0117** (`tools/sync-fuxi-adsp.py`; 13 .c into bsd/conf/files after sweet_usb.c,
    headers to bsd/dev; LICENSE.UniXNU-sheng already shipped by 0098, checked identical). Port of UniXNU-sheng
    6880068 AdspProbe.cpp / AdspFirmware.c / ShengBattery.cpp; Qcom*.c, FatVolume.c, ShengBattmgr.c, ShengQrtr.c copied
    unchanged. Differences from sheng: IPCC/RSC polled (SPI 229 and SPI 5 masked in the GICD first: no handler here),
    windows via kernel_map VA + pmap entries (never ml_io_map), carveout windows unmapped by pmap_remove before
    auth_and_reset with the VA kept, firmware only from modem_a/modem_b in place, refused (survey only) with fuxiwlan.
  - 0106 (`sweet_ufs.c`): read-only `fuxi_ufs_part_find` / `fuxi_ufs_part_read` for GPT partitions on LUNs 1-5
    (LUN switched for one READ(10) under sweet_ufs_lock and restored; FUXI SCSI policy unchanged; refused with
    fuxi_unixnu_ufs=1); `fuxi_adsp_init()` called after `fuxi_wlan_init()`.
  - Modes: `fuxiadsp=N` or ADT `/chosen` u32 `fuxi,adsp` (the boot-args field is 607/608 full): 1 survey (reads only),
    2 start the DSP (SMP2P, AOSS load_state, lcx/lmx proxy votes on TCS 1, PAS 36 + PAS 1, GLINK, IPCRTR locator,
    PMIC_RTR_ADSP_APPS) and log `FUXI BATTERY:` lines, 3 also SweetBattery publishes the readings (phase 2: percent
    >= 1, never critical, charger state only when known; xnu/fuxi-adsp/SweetBattery.cpp + fuxi_adsp_battery.h, the
    0062 file plus the new source, carried by 0117). docs-lanes/adsp.md has the stages and log lines.
- Checks: NDK clang 17 `--target=arm64-apple-ios16.0 -ffreestanding -fsyntax-only` with stub headers
  (`work\adsp-battery\hostcheck`) clean for all 13 C files (-Wall -Wextra -Wshadow -Wsign-compare); bsd/conf/files
  chunks of all patches applied in order on the real xnu-8796.141.3 list (fetched read-only) - 0117 lands after
  sweet_usb.c; Alpine guest test (`work\adsp-battery\guest-run-fw.py` + `fwtest-share`, modem_a backup attached
  read-only as vdc): the kernel's fuxi_adsp_fw.c + FatVolume.c + QcomMdt.c read adsp_dtb (16184 B, 1 load) and adsp
  (39721400 B, 57 loads, hash = phdr 58) byte-identical to an independent Python join, metadata 4044 / 8524 B
  identical, placements fill 0x20b8 / 0x4680000 of the carveouts, every UFS read inside the partition (588 reads).
- CI: 43613488060 failed only at sync-fuxi-prof --check (0111 offset) -> fixed; **43613785715 OK** (phase 1: all 13
  files compiled, 0 errors, only xnu-wide style warnings); 43614748612 (phase 2) running.
- Windows notes: upstream sync/check tools need `run_lf.py` AND `GIT_CONFIG_PARAMETERS="'core.autocrlf=false'"` (git
  apply in temp dirs otherwise writes CRLF and the byte compares fail); sync-fuxi-sys / sync-fuxi-wifi-ios fail on
  Windows at the base too. The Bash tool turns `\\n` inside heredocs into real newlines: write such scripts with the
  editor.
- Loader tool: `Codex-Xiaomi13-GPU-20261109\prepare-adt-variant-loader.py <out> <src loader> <artifact> chosen:fuxi,adsp=N`
  (prepare-kernel-variant-loader.py plus /chosen u32 properties; same payload/loader checks). Not run yet.
- On-device plan (when the phone is free and the user agrees): bl4 + adsp kernel, mode 1, then 2, then 3, each a
  fastboot boot (no write). ADT root extents must match the phone (verify-native-map). Look for `FUXI ADSP: survey`,
  `firmware volume modem_a`, `ready after N ms`, `channel PMIC_RTR_ADSP_APPS up`, `FUXI BATTERY: percent=`; compare
  with Android `dumpsys battery` before mode 3.
- **Kernel CI 43614748612 OK** (phase 2, harness 79948d9 = ksrc 478bf32): SweetBattery.cpp compiled (only its existing
  OSDefineMetaClass warning), no errors. Artifact `fuxi-kernel-adsp-battery` (23347563 B, sha256:d41c0aee...) unpacked to
  `Codex-Xiaomi13-GPU-20261109\kernel-adsp-battery-artifact`: SHA256SUMS OK, cmdline.txt = kernel-bo4096's, kernel sha256
  `e45f9609080afbf8d5d380c9f8121903d9b327c918447fd7298beab8cc6c9ae4` (has the FUXI ADSP / FUXI BATTERY / SWEET BATTERY: ADSP
  / fuxi,adsp / PMIC_RTR_ADSP_APPS strings; also the backlight code, off without fuxibacklight=4).
- Board facts re-checked against the Fuxi live DTB: apps_rsc drv@2 tcs-offset 0xd00, tcs-config ACTIVE x3 first (TCS 1
  is active), IPCC SPI 229 level; smp2p-adsp qcom,smem <443 429>, remote-pid 2, IPCC client 3 signal 2 (SMP2P) / signal 0
  (GLINK); SMEM hwlocks <&tcsr_mutex 3> (syscon 0x1f40000, stride 0x1000); AOSS QMP 0xc300000+0x400; ADSP proxy supplies
  lcxlvl/lmxlvl (DT level 384; the port votes the highest level as sheng does, dropped at handover). The six pd-mapper
  files in modem_a (adspr/adsps/adspua/battmgr/cdspr/modemr.jsn) match ShengQrtr's locator table exactly.
- Loaders (host only, NOT booted): `native-adsp{1,2,3}-user-loader` = prepare-adt-variant-loader.py from
  native-vt6tc-user-loader (same 592-char args, NO fuxibacklight, so ADSP is judged alone) + kernel-adsp-battery-artifact +
  ADT /chosen fuxi,adsp=N. boot.img sha256: 1 `864807e7...1c31`, 2 `b5105f42...c301`, 3 `3a0ce88a...9574`. All three pass
  `verify-loader-safety.py` (new: integrity + kernel = artifact + ADT root extents = managed-native-map-current.json + 20 GiB
  blocks; works for any variant, unlike the stale verify-args-on-loader-payload.py) against the 00:10 mapping.
- Boot tooling: `boot-exp-v.ps1` = boot-exp.ps1 + `-Verifier` (default unchanged; the original is untouched because
  other sessions use it); `work\adsp-battery\run-adsp-boot.ps1 -Mode N` runs it with verify-loader-safety.py, kernel
  kernel-adsp-battery-artifact, and writes `<out>\adsp-summary.txt` (FUXI ADSP/BATTERY/UFS lines) and the Android
  `dumpsys battery` after the return. It refuses while run-monitor/boot-exp/monitor/pull/deploy processes run.
- **Sound step 1, `fuxiamp`** (ksrc 417dcf3, harness a37ccac, kernel CI 43616686724): `xnu/fuxi-adsp/fuxi_amp.c` + UniXNU-sheng
  `GeniI2c.c/h` (unchanged) in 0117; called from `fuxi_adsp_init` with its own switch (`fuxiamp=N` / ADT `fuxi,amp`). Board:
  live DT i2c@880000 = QUP2 SE0 (SPI 373, `dmas` present), cs35l41@40 "B" reset GPIO 183, @42 "T" reset 182, SDA gpio0 / SCL
  gpio1 function 2 (`qup2_se0_l0/l1_mirb`, Linux pinctrl-sm8550.c v6.12), 2 mA pull-up - the same engine and pins as sheng's
  amplifier bus 2. GCC (gcc-sm8550.c v6.12): 0x52010 bits 0-4 = wrap2 core/s_ahb/m_ahb/core_2x/s0, halts 0x233d4/0x233d0/
  0x233cc/0x23004/0x1e004, SE0 RCG 0x1e010. Mode 1 reads only (engine registers only when all five branches read CLK_OFF=0:
  an unclocked read stalls the bus) and answers whether the engine is FIFO-capable or GSI-only (`GENI_IF_DISABLE` bit 0, as
  touch's SE4). Mode 2 = sheng's setupClocks/setupPins/setupBus + SPI 373 masked + per amp reset release, DEVID/REVID/
  IRQ1_STATUS3/4 reads, back into reset, pins and votes restored; nothing written to the amplifiers. docs-lanes/adsp.md
  "Speaker amplifiers". Host syntax check clean. Kerfand/UniXNU-Fuxi-Arm64e-iOS17 `device/package/FuxiCs35l41.c`
  (BSD-2-Clause-Patent, CS35L41 init programs on sheng's runner) is the reference for the later amp configuration.
- **Kernel CI 43616686724 OK** (fuxiamp, harness a37ccac): fuxi_amp.c and GeniI2c.c compiled (style warnings only).
  Artifact -> `Codex-Xiaomi13-GPU-20261109\kernel-adsp-amp-artifact` (SHA256SUMS OK, cmdline = bo4096's), kernel sha256
  `0585ca64c537f8109886a3e9e9017b4104765f72edf0915fbd169f133263c64b` (ADSP phase 1+2, fuxiamp, backlight code).
- Loaders (host only, NOT booted; same 592-char vt6tc-user args, no fuxibacklight; pass verify-loader-safety.py):
  `native-survey-user-loader` = ADT fuxi,adsp=1 + fuxi,amp=1 (both read-only: one boot shows the DSP's SMP2P/AOSS/RSC
  state and the amplifier bus' clocks/pins/FIFO lock), boot.img `9eecd7f5...6c3b`; `native-amp2-user-loader` = fuxi,amp=2,
  boot.img `a2ac4274...21e0`. `work\adsp-battery\run-adsp-boot.ps1 -Loader <name>` now takes any loader (kernel from its
  source.txt). Suggested order when the phone is free: survey -> adsp2 -> amp2 -> adsp3 -> bl4 (brightness).
- At 01:2x the hybrid lane was running 0028 boots (native-layout0028-hyb1-p1-boot-1), so nothing was booted. D: 18 GB free.
- **Pull 20261115** (00:10-00:17): phone after the 0027 boot = sha256 `dc5d74744ccf0b3dd3194492eabc643b7f49ffd2b4b6cef9825ed76f7cd39b6a`
  (391 blocks vs 0025 kept in `fuxi_Backup\ios-root-phone-20261115`, README index updated; 168 blocks vs 0027). Note:
  the Bash tool collapses a doubled backslash to one inside heredocs, so a Python heredoc wrote `\n` into a path (`...images\native...`
  became a newline); fixed. Write Python edit scripts to a file instead of heredocs when they contain backslashes.
- **0028** = phone state dc5d7474... + bridge `921b1fcc...` (CI 43614089135 at 1a6fe04, ZIP = GitHub digest
  b8f5cd50...): `images-build\native-layout0028-root.img` sha256 `ae99988992d079ea6cdccd16f4618f9ff357e82b4149d8146d6eb445ca3ae282`
  (journal replayed, fsck x3 OK). Deployed 00:30-00:37 (19 blocks, READBACK_MATCH) by `hybrid-driver\chain-0028.sh`
  (build -> deploy -> boot, unattended).
- **Boot `native-layout0028-hyb1-p1-boot-1`** (00:37, same loader/args as 0027's): Settings opened, Android back, 13271
  lines, tsens max 64.7 C. With 26 marks the census costs more: 35.7 us/draw (0027: 29.6), 81.8 Hz, 6.64 ms CPU/frame,
  so read the splits as proportions. T1 2.13 M draws: pre 2.8 + settle 4.8 + inst 3.0 + rest 24.1 + post 0.9, between
  20.8. **pre**: settings 1.1, tables 1.0, ext 0.5, colors 0.2. **settle**: refresh 2.2, shape 1.0, lookup 0.9,
  fields 0.5, fit 0.3; bind shape reshaped 50.2 % (kept 49.8 %, found 49.7 %, made 0.5 %). **rec 11.9**: record 6.8
  (the Vulkan recording loop; the 0027 profile puts ~80 % of Recording::record in Turnip calls, vkCmdDrawIndexed the
  largest), retain 2.1, tail 1.5, setup 0.8, notes 0.3, keep 0.2, prep 0.2 (keep_draw / keep_capture are cheap: their
  lists are only live on devices or passes that need them). rest: bind 3.4, uses 2.5, tables 1.3, instance 1.2, fixed 1.0,
  notices 0.9, pipe 0.9, shadows 0.6, heap 0.4.
- Reading of the code for the first cut: every draw locks the device's resource IDs and upgrades every live sampler
  state's Weak (an atomic CAS each) into the argument sampler list, drops them all again at the end, and compares the
  live bind state's listing element by element (`heap_sets::lists`); the list is part of every bind shape
  (ArgumentSamplersShape), so it cannot simply be skipped for pipelines without a heap. draw_settings depends on the whole
  EncoderState (+ layout, amplification, visibility, rate map) and announces; caching it needs a state epoch (later).
  CA averages ~3 draws per render encoder (32 passes per job), so per-encoder memos barely hit; per-pipeline ones do.
- **metal2vulkan patch 0013** (`m2vb` 798b2dd, harness b402060, `patches/metal2vulkan-fuxi-0013-lane-samplers.patch` =
  `git -C m2vb diff ba0a2de 798b2dd`, 0012 regenerated byte-identical): lane switch `fuxi_census::lane()` = fuxihyb>=2,
  three exact cuts: (1) the encoder keeps its argument sampler listing while `SamplerIdReads::slot_changes` (new, bumped
  under the IDs' lock in IdHold::hold / drop of a sampler slot) is unchanged and no fit_samplers shrank it; the encoder's
  end drops it (sampler states live at most one encoder longer); (2) `LiveBind::argument_samplers_at` lets the refresh skip
  `lists()` when the live state was last brought to the same untouched listing (executions' precompile passes None and
  clears it); (3) `GraphicsState::settle_shape` scans by Arc address before comparing contents and leaves a front entry in
  place. Census `lane` line: argument samplers per draw, Vulkan state commands per draw, set0 / set0same (draws whose set-0
  writes FNV-hash equal the last of their pipeline layout), layouts; event samplerskept. Frontend CI 43621663423.
- Loaders (host): `native-hyb2-p1-loader` = native-p1-vt6tc-loader +fuxihyb=2 (payload differs from hyb1's by 1 byte;
  source/swaps re-based on native-page1-spin-loader like hyb1's, originals kept as *.vs-p1vt6tc); `native-tour-hyb2-loader`
  = native-vt6tc-user-loader with fuxiperfidle=1000=>fuxitour=40, fuxitcache=1=>fuxihyb=2, +fuxitourhold=30 (args 599;
  source/swaps re-based on native-draws-spin-loader, originals *.vs-vt6tc). The @3 script prints a `FUXI TOUR: shot N step`
  at every step, so -TourShots must not be used with the p1 loaders (180 framebuffer reads).
- Running unattended: `hybrid-driver\pull-phone-20261115b.sh` (in place over the 0028 raw; delta vs 0025 to
  `fuxi_Backup\ios-root-phone-20261115b`) and `hybrid-driver\chain-0029.sh` (CI -> digest check -> build-0029 ->
  deploy-layout0029 with the pulled sha -> boots hyb1 p1, hyb2 p1, tour hyb2 with shots).
- **Pull 20261115b** (01:33-01:40): phone after the 0028 boot = sha256 `30451ef70c93f167b4315a77d64bf042b948ea4f5191ce2c947096e6065e8d83`
  (398 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115b`, index updated; 163 vs 0028).
- **0029** = phone 30451ef7... + bridge of CI 43621663423 (patch 0013, ZIP = digest 4df48f85...):
  `images-build\native-layout0029-root.img` sha256 `29d6d426cde2fd5c5336cf4ae522a9f996e0306597221d650c29a97adca53c3b`;
  deployed 01:54-02:02 (20 blocks, READBACK_MATCH) by `hybrid-driver\chain-0029.sh`.
- Boots (all unattended):
  - `native-layout0029-hyb1-p1-boot-1` (02:02, fuxihyb=1): the monitor lost the probe at script loop 6/20
    (`usb.core.USBError: [Errno 2] Entity not found`, no panic or backboardd crash in the 5445-line log; the same
    error ended earlier boots too: 0025 vt6tc-user, 0024 profinc/profwait/profedge/base2323), Android came back after
    the reset deadline. 9 census windows: 31.7 us/draw.
  - `native-layout0029-hyb2-p1-boot-1` (02:12, fuxihyb=2): complete, 30 windows: 34.4 us/draw; samplerskept 89 % of
    draws. **The lane line shows only 2.0 argument samplers per draw**, so 0013's sampler cuts are tiny; 6.7 Vulkan
    state commands per draw; set0same 22-23 % (writes equal to the last of their pipeline layout, 62-64 layouts).
  - `native-layout0029-tour-hyb2-boot-1` (02:23, native-tour-hyb2-loader): probe lost during shot 4; shot 3
    (app-library) static and correct (frosted folders over the blurred wallpaper, icons, search field); shots 1-2 moving.
  - **Boot-to-boot drift is larger than the cuts**: 0028 hyb1 (375 s) 35.7 vs 0029 hyb1 (first 94 s) 31.7 us/draw for
    the same configuration. Hence fuxihyb=3 below.
- **metal2vulkan patch 0014** (`m2vb` ebf808d, harness 394e286, `patches/metal2vulkan-fuxi-0014-lane-sets.patch` =
  `git -C m2vb diff 798b2dd ebf808d`, 0013 regenerated byte-identical): (1) under the lane, set-0 descriptor set reuse:
  `DescriptorArena` (per command buffer) keeps the latest 32 written sets with their writes flattened to u64 words
  (set/binding/element/type and every handle/offset/range/layout); a draw whose words equal one of them for the same set
  layout (every write set 0, none holding an object) binds that set instead of allocating and writing; cleared with the
  pools; event `setreused`. (2) `fuxihyb=3`: lane alternates per census window (T1's report flips it; window line gets
  `lane=0|1`). `hyb-census.py` prints on/off deltas per phase and split and a same-pipeline paired comparison (top lines),
  and treats samplerskept/setreused as counts. Frontend CI 43628206211. Loader `native-hyb3-p1-loader` (+fuxihyb=3,
  re-based like hyb1/hyb2).
- Running: `hybrid-driver\pull-phone-20261115c.sh` (over the 0029 raw; delta to `fuxi_Backup\ios-root-phone-20261115c`),
  `hybrid-driver\chain-0030.sh` (CI -> digest -> build-0030 -> deploy-layout0030 -> hyb3 p1 590 s -> tour hyb2 shots).
- Disk: D: ~16 GB free before the 20261115c pull; each pull keeps ~1.6 GB in fuxi_Backup. The deltas 20261114,
  20261114b, 20261115 (and soon 20261115b) are superseded by the newest; deleting them needs the user's OK.
- **Pull 20261115c** (02:44-02:51): phone after the three 0029 boots = sha256
  `82808d65f673a6beb8fc595221e282f0203431a1400dae485647730a30d52f30` (430 blocks vs 0025 in
  `fuxi_Backup\ios-root-phone-20261115c`, index updated; 233 vs 0029).
- **0030** = phone 82808d65... + bridge of CI 43628206211 (patch 0014, ZIP = digest 44e73597...):
  `images-build\native-layout0030-root.img` sha256 `fa8d08df7e148019d82cb9f94b09f148f8fb29cf5a064dfba544b8cd0c45a07a`;
  deployed 03:07-03:14 (19 blocks, READBACK_MATCH) by `hybrid-driver\chain-0030.sh`.
- **One-boot A/B `native-layout0030-hyb3-p1-boot-1`** (03:14, native-hyb3-p1-loader, 590 s; complete, 31 windows,
  Settings opened): off 15 windows / 1.03 M draws, on 16 / 1.00 M. **tot off 35.11, on 35.87 (+0.76 us/draw);
  same-pipeline pairs (19 pipelines, 0.8 M draws per mode): +0.59 us (+1.8 %)**. By piece: rest.bind +0.82 (the set-0
  reuse: setreused 10.1 % of all draws = ~20 % of lane draws, while every lane draw flattened its writes into a Vec and
  searched up to 32 kept sets), rec.record +0.02 (Turnip did not get cheaper: a reused set is rarely the bound one),
  pre.tables -0.17 (the kept argument sampler listing), everything else within +-0.1. So the method resolves ~0.5 us,
  the set reuse is a net loss, the sampler listing a small win.
- Tour `native-layout0030-tour-hyb2-boot-1`: probe lost again (3416 lines); not analysed (the set reuse is gone).
- **Turnip on the display thread** (sweetprof of the hyb3 boot, display.primary t508, pc/lr-level counts; bridge slide
  0x108a68000, Turnip 0x109e14000 by macho-fit 14/14; symbols from `llvm-nm` of the 0027 share's dylib): QueueSubmit path
  ~290 (vk_common_QueueSubmit -> vk_queue_flush -> queue_submit -> darwin_queue_submit, the commit), **tu_CmdBindPipeline
  +0x240 261, tu_CmdDrawIndexed +0x64 258** (-> tu6_draw_common 145), vk_common_ResetCommandPool 137,
  ResetCommandBuffer 100, vk_dynamic_graphics_state_copy 92 (from the pipeline bind), CmdEndRenderPass 85, against
  m2v's draw ~5228. A pipeline bind (90 % of draws) costs Turnip about what a draw does: tu_bind_vs/fs..,
  vk_cmd_set_dynamic_graphics_state (compare + copy of the whole dynamic state), CP_SET_DRAW_STATE of 10+ groups
  (`work\mesa-ref\tu_cmd_buffer.cc`). That is L2's target, not L1's.
- **metal2vulkan patch 0015** (`m2vb` 86bc93a, harness bb74442, `patches/metal2vulkan-fuxi-0015-census-calls.patch` =
  `git -C m2vb diff ebf808d 86bc93a`, 0014 regenerated byte-identical): removes the set-0 reuse; the 34 bind-call hooks
  become `fuxi_census::setter(kind)` guards timing each outermost call to its return; render_command_encoder,
  end_encoding and CommandBuffer::commit are timed; open_render counts encoders that log their calls (rebinds);
  census `calls` line: setters (us per draw), per_draw (calls), setter_us (per call), encoders, open, end (us per
  encoder), commits, commit (us), logging. Frontend CI 43633647543.
- Running: `hybrid-driver\pull-phone-20261115d.sh` then `hybrid-driver\chain-0031.sh` (hyb3 p1 590 s only).
- **Pull 20261115d** (03:44-03:51): phone after the 0030 boots = sha256 `01b94862eae27aa5188f41dbb5f0fa51c67e489aa0fb09dff5432e73e5b88409`
  (461 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115d`, index updated).
- **0031** = phone 01b94862... + bridge of CI 43633647543 (patch 0015, ZIP = digest cf32784f...):
  `images-build\native-layout0031-root.img` sha256 `634fac4f880902781ad80e8b57de83865ece22277f75f84d4b899b23d9ddaa54`;
  deployed 04:04-04:11 (20 blocks, READBACK_MATCH) by `hybrid-driver\chain-0031.sh`.
- **Boot `native-layout0031-hyb3-p1-boot-1`** (04:12, hyb3, 590 s asked): probe lost after ~6 min (8826 lines, 14 windows,
  same USB error as before). **calls line: setters 2.29 us/draw (3.98 calls, 0.57 us each); render encoders 741/s, open
  25.2 us, end 71.7 us each; commits 68/s, 305 us each; logging encoders 107077 = every encoder** (rebinds.any() is true:
  the process has buffers over host pages, so every encoder logs every call for a rebind that the NativeMetal frontend
  never makes, `NativeMetalObjects.h`: "This frontend does not rebind/remap live backing ranges"; m2v's
  `DeviceOptions::refuse_rebinds` exists for exactly that consumer and turns the logs off). Per second on the display
  thread (on CPU ~50 %): draws ~236 ms, between draws ~150 ms (setters 15.5 of it), encoders ~72 ms, commits ~21 ms.
- The lane A/B again came out +0.75 us/draw with almost nothing in the lane: the turns aliased with the Settings script's
  ~18.7 s loop (two 10 s windows), so "on" and "off" sat on fixed halves of the loop. 0016 picks windows at random.
- Deleted (my own outputs, superseded, re-downloadable from CI): `ci\native-layout0028..0030-artifact` and `-share`
  (~470 MB). D: 13 GB free before the 20261115e pull.
- **metal2vulkan patch 0016** (`m2vb` 6a2d2dd, harness 97d83ab, `patches/metal2vulkan-fuxi-0016-lane-logs.patch` =
  `git -C m2vb diff 86bc93a 6a2d2dd`, 0015 regenerated byte-identical): under the lane `DeviceShared::logs_calls()` is
  false (render/compute/blit encoders log nothing) and a lane boot (fuxihyb=2|3, `fuxi_census::lane_configured`) refuses
  every rebind in `buffer_descriptor.rs` as refuse_rebinds does; `Spares::give_command_pool_reset` resets a command pool
  on the completion thread as the command buffer's work ends (lane only, not in an exit section), and `command_pool()`
  skips the reset for such a pool; fuxihyb=3 picks each window's way with xorshift64*; census `open` (4 marks in
  open_render) and `end` (10 marks through end_pass) lines, pieces p0..p9 and the tail p10 per encoder.
  Frontend CI 43637621979.
- Running: `hybrid-driver\pull-phone-20261115e.sh` then `hybrid-driver\chain-0032.sh` (hyb3 p1 590 s, tour hyb2 shots).
- **Pull 20261115e** (04:29-04:36): phone after the 0031 boot = sha256 `134f5b2b44a044c6b19d4284d89b1bed5abdd5f0cc71e298f6ba7bdd4487c514`
  (467 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115e`, index updated).
- **0032** = phone 134f5b2b... + bridge of CI 43637621979 (patch 0016, ZIP = digest 8b574a1c...):
  `images-build\native-layout0032-root.img` sha256 `4d23c56b59a22b89871b916a039fbe5d16bd8cae2db40ee43c70d395b4d70e68`;
  deployed 04:46-04:53 (20 blocks, READBACK_MATCH) by `hybrid-driver\chain-0032.sh`.
- **One-boot A/B `native-layout0032-hyb3-p1-boot-1`** (04:53; probe lost after ~7 min, 18 windows: 7 off / 11 on at
  random). **The lane is now a clear win: tot off 34.97 -> on 30.68 us/draw (-4.29), same-pipeline pairs -4.49 us
  (-13.8 %), between draws -2.00, setters -0.77 us/draw, encoder open 26.3 -> 17.9 us, end 73.9 -> 69.0 us, commit 307
  -> 279 us.** The savings spread over every piece (rest.rec -1.34 of it: record -0.80, tail -0.45; pre.tables -0.30;
  settle.refresh -0.24; inst -0.57): no log pushes and Arc clones per call, less allocation. Roughly 11 % of the display
  thread's CPU at this load (draws ~30 ms/s, between ~14, encoders ~11, commits ~2).
- Encoder splits (both modes, 146.8 k encoders): **open 21.0 us** = p0 0.6 (log, checks) + p1 5.6 (surface stencil loads,
  render_pass::layout) + p2 1.4 (rate map, shape, compatible pass) + p3 5.3 (begin_draws: secondary command buffer) +
  tail 8.0 (keeping, core, extended shadows, the encoder's construction). **end 70.8 us** = p0 2.3 + p1 0.2 + p2 2.7 + p3
  1.4 + p4 1.5 + p5 5.8 (uses, copies, scratch, clears planning) + p6 5.7 (render pass + framebuffer per instance) + p7
  12.1 (resolve instance, resolves, tile dispatch staging, visibility, shadow/extended/reformat copies, steps before) + p8
  2.7 (instance steps) + **p9 17.8 (core.command over the pass's uses + vkCmdBeginRenderPass / ExecuteCommands /
  EndRenderPass: Turnip's GMEM tile emission sits here)** + **tail 18.5 (resolves after, scrubs, retains,
  end_with_replay)**. CA averages ~9 draws per encoder (7312 draws/s over 796 encoders/s).
- USB probe loss: most boots since 0029 lose the probe after 5-7 min (`Entity not found`); the `CRASH vmd sig 9` before it
  is periodic in every boot (vmd is in sweetnoexec), not the cause; the phone keeps running (its reset deadline fires).
  Tours 0030/0032 lost the probe before or at the first shots. Not chased further tonight.
- Deleted my superseded `ci\native-layout0031-artifact` / `-share`. D: 11 GB before the 20261115f pull.
- **metal2vulkan patch 0017** (`m2vb` 161e1bd, harness a90d2d4, `patches/metal2vulkan-fuxi-0017-lane-quiet.patch`,
  0016 regenerated byte-identical): `fuxihyb=4` = lane on, census off (census only for 1..=3); end marks 10-15 (after the
  resolves/copies/results, before the whole-pass note, before the retains, end_pass's return, end_with_replay's shadow
  copies, vkEndCommandBuffer; render encoders only), open marks 4-5 (core/extended shadows, construction). Frontend CI
  43642453133. Loader `native-hyb4-p1-loader` (+fuxihyb=4, re-based like the others).
- Running: `hybrid-driver\pull-phone-20261115f.sh` then `hybrid-driver\chain-0033.sh`: hyb3 A/B 590 s, then
  `native-p1-vt6tc-loader` (no fuxihyb, today's behaviour) 420 s vs `native-hyb4-p1-loader` 420 s, census-free, for Hz
  and CPU per frame (`draws-census.py`).
- **Pull 20261115f** (05:23-05:30): phone after the 0032 boots = sha256 `6f622a275fcbcca15f0e260ad185ba1f031a9777372288f3c2d54591b44d7a9c`
  (delta vs 0025 in `fuxi_Backup\ios-root-phone-20261115f`).
- **0033** = phone 6f622a27... + bridge of CI 43642453133 (patch 0017, ZIP = digest 368e4acb...):
  `images-build\native-layout0033-root.img` sha256 `ce61614fc9768fbafe64f5225841fdd63a58c316bb2f751bfd1d9c689ba8db9b`;
  deployed 05:46-05:54 (19 blocks, READBACK_MATCH) by `hybrid-driver\chain-0033.sh`. **Phone root is now 0033.**
- All three boots ran to the end (no probe loss this time):
  - **`native-layout0033-hyb3-p1-boot-1`** (fuxihyb=3, 32 windows: 13 off / 19 on at random): **tot off 36.23 -> on
    29.17 us/draw (-7.05), same-pipeline pairs -6.95 us (-20.6 %)**, between -3.89, setters -0.97 us/draw, encoder open
    27.6 -> 17.3 us, end 77.0 -> 67.0 us, commit 320 -> 266 us; rec.record -1.14, rec.tail -0.49.
  - **Census-free A/B (draws-census.py, same Settings script, 420 s each)**: `native-layout0033-p1-vt6tc-boot-1` (no
    fuxihyb = today's behaviour) **81.2 Hz, display thread on CPU 54.3 %, 6.68 ms CPU/frame, 66.4 us per draw all-in**;
    `native-layout0033-hyb4-p1-boot-1` (fuxihyb=4, lane without census) **87.1 Hz, 48.6 %, 5.58 ms CPU/frame (-16.5 %),
    55.7 us per draw**. Settings opened in both.
  - Encoder splits (227 k encoders): open 21.7 = p0 0.7 + p1 5.5 (layout) + p2 1.4 + p3 6.1 (begin_draws) + p4 5.0
    (Keeping::of + the core) + p5 0.4 + construction 2.6; end 71.3 = p0 2.3 + p2 2.8 + p3 1.3 + p4 1.5 + p5 5.7 + p6 5.7
    + p7 11.8 + p8 2.8 + p9 17.3 (core.command + Turnip begin/execute/end render pass) + p10 0.2 + p11 0.6 + p12 1.6 +
    **p13 11.2 (core.retain of the pass's objects and end_pass's return: every field of the encoder not moved out and
    every local dropped here)** + p14 1.0 (end_with_replay's shadow copies) + p15 0.5 (vkEndCommandBuffer) + tail 4.7
    (merging into the command buffer's state). CA: 458 encoders/s, ~9 draws each, 42 commits/s at this load.
- Next candidates: defer the encoder's leftover drops (p13) to the command buffer's completion (retain them), the
  recording block p9 (Turnip GMEM), resolve planning p7; Turnip's pipeline bind (L2).
- `fuxi_Backup\README.md`: a Bash-heredoc edit inserted a duplicate 20261115e row (the backslash collapse again);
  fixed by a Write-made script: 20261115f row correct, the "phone now" note says 0033.
- Loaders (host only): `native-hyb4-user-loader` = native-vt6tc-user-loader +fuxihyb=4 (args 602, boot.img
  `2e482a92f2229f4f59d2d01c4ea8ca3bde2d320bd5228958b0ae37fa3d0c1ace`; the daily loader with the lane, not booted yet;
  the KSU slot-b chain still carries the old args); `native-tour-hyb4-loader` = tour + fuxihyb=4 (args 599).
- **metal2vulkan patch 0018** (`m2vb` ff620b3, harness 0ee2c0a, `patches/metal2vulkan-fuxi-0018-lane-leftovers.patch`,
  0017 regenerated byte-identical): end_pass names the 25 encoder fields it does not read (state, pipeline, routed, the
  five argument tables, keeping, drawn_before, unordered(+dispatches), pass_start, live, quiet_bound_values,
  draw_commands, uses/argument-sampler rooms, bind_memo, last_retained, amplification, factors, rate_map,
  extended_written, zero_indices); under the lane `core.retain(leftovers)` (dropped on the completion thread with the
  command buffer), else dropped as before. nmb_command_buffer_uses_* read used_resources(), not retained objects, so
  the extra holds change no dependency answer. Frontend CI 43648986405.
- Deleted my superseded `ci\native-layout0032-artifact` / `-share`.
- Running: `hybrid-driver\pull-phone-20261115g.sh` then `hybrid-driver\chain-0034.sh` (hyb3 A/B 590 s, tour hyb4 shots).
- **Pull 20261115g** (06:37-06:44): phone after the 0033 boots = sha256 `40272c6e5b7203223b85d57e8f3c837481d8ff8d968e558f93e61977fde71cc7`
  (505 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115g`, index updated).
- **0034** = phone 40272c6e... + bridge of CI 43648986405 (patch 0018, ZIP = digest f5998533...):
  `images-build\native-layout0034-root.img` sha256 `31edbabea35924eb8887d3a44eac9f5bc9476d5ddb277aa9475b3007a8fd1ee1`;
  deployed 06:56-07:03 (19 blocks, READBACK_MATCH). **Phone root is now 0034.**
- **`native-layout0034-hyb3-p1-boot-1`** (complete, 32 windows, 16 off / 16 on): tot off 36.32 -> on 31.15 (-5.18 us/draw),
  **same-pipeline pairs -5.55 us (-16.4 %)**, between -3.78, setters -0.78, open 27.9 -> 18.6, **end 78.7 -> 67.0 (-11.7;
  0033 gave -10.0, so 0018 is worth ~1.7 us per encoder)**, commit 323 -> 277. End split p13 now 8.0 (both modes mixed).
  The lane's draw delta varies between boots (-7.05 in 0033, -5.18 here); same-pipeline -16 to -21 %.
- **`native-layout0034-tour-hyb4-boot-1`** (tour + fuxihyb=4, i.e. the daily lane without census): 10 shots before the
  probe was lost at ~shot 11 (Android came back after the reset): lock (date/time/battery/camera button), home,
  app-library (static), app-library-idle, today (caught the Settings open transition: blurred backdrop, rounded card),
  spotlight, control-center (frosted modules, brightness slider), settings, general, about. **All looked correct**
  (checked lock, control-center, today, settings by eye; "moving" rows are the clock/home-bar/animation bands).
- D: 6.4 GB free after the 20261115g pull: no further pull/deploy until older fuxi_Backup deltas are cleared (user's OK).

## 2026-11-15 ~08:50 (continued, user: "刪除舊備份並繼續", then "不要中斷不要問我")

- Deleted the superseded phone deltas `fuxi_Backup\ios-root-phone-20261114` .. `20261115f` (user's OK), D: 20 GB free.
- **Pull 20261115h** (09:03-09:10): phone after the 0034 boots = sha256 `298672236b6e5fe173c1e0cbc978708a4ff865175e6e4f555f423831f6c3f0e5`
  (515 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115h`; 20261115g deleted as superseded, index updated).
- Census overhead seen in the 0034 hyb3 profile (sweetprof, display.primary t508): the single most frequent on-CPU stack
  (443/30000) is `mach_absolute_time+0x30` (right after its `isb`) <- clock_gettime_nsec_np <- clock_gettime <- the
  bridge's `Timespec::now`; ~10-20 % of the census boot's display-thread samples carry clock_gettime. The commpage user
  timebase is on (type 1: isb + CNTVCT_EL0, no trap); the marks are just many. Census-on numbers stay relative only.
- Census-free profile (0033 hyb4 boot, t532, slides bridge 0x10c46c000, Turnip 0x10d81c000, FuxiNativeMetal
  0x109f0c000): RenderCommandEncoder::draw 4447 inclusive -> draw_resident 3940 (Recording::record 581, settle_draw 193,
  retain_draw 183, memmove 169, ArgumentTable::retain 74, the rest inline in draw_resident), endEncoding 1014, commit 596,
  render_command_encoder 224-272; CA: capture_backdrop 2387, GaussianBlur 1398, narrow_blur+process_edges ~800.
- **The frame chain is at the edge of the vsync budget**: 0033 hyb4 `script-metric.py`: gpu_ms mean 4.27, CPU 5.58 ms/frame,
  + ~0.6 ms copy = ~10.45 ms from heartbeat to copy done, while `fuxivsynctoff=6000` gives ~10.54 ms (heartbeat = TE +
  offset, deadline = TE two periods on). Flip gaps 80 % single, 3102 two-period gaps. Only 6000 was ever tested (7800
  default before). Loaders `native-hyb4-p1-vt4-loader` (6000=>4000, boot.img 7a94d247...) and
  `native-hyb4-p1-vt2-loader` (6000=>2000, boot.img 6ca1bd3f...) from native-hyb4-p1-loader by prepare-args-on-loader.py.
- Running: `hybrid-driver\vt-ab-0034.sh` = hyb4 at offsets 6000, 4000, 2000 on root 0034 (420 s each, @3 script).
- `vt-ab-0034.sh`: the 6000 boot ran (`native-layout0034-hyb4-vt6-boot-1`: **86.4 Hz, CPU 5.01 ms/frame, gpu_ms 4.31,
  flips 84 % single**; flog: copy_submit->fence mean 4284 us, GPU submit_to_copy_start p50 3831 us, i.e. the frame's GPU job
  runs after the swap). The 4000/2000 boots stopped at `verify-args-on-loader-payload.py` (fuxiperf=/fuxiearly= inherited
  but not in swaps.txt): re-based like the hyb loaders (source.txt/swaps.txt from native-hyb4-p1-loader with the offset
  swapped; originals kept as `*.vs-hyb4`), failed exp logs moved to `Codex-...\failed-verify-0034\`; `vt-ab-0034b.sh` runs
  4000 then 2000.
- **Why the GPU time is serial**: CA encodes the whole frame (~11 encoders) into one command buffer and commits it at the
  end (commitAndWaitUntilSubmitted right before the swap), so the GPU starts only then: heartbeat -> CPU 5 ms -> GPU ~4 ms
  -> copy 0.6 ms. The darwin backend has 8 kernel job slots and the kernel is in queue mode (fuxigpuncimport bit 9), so
  several submissions per frame do not block the CPU.
- **nativemetal patch 0016 (split commits)**, source = `work\nmb\native` (git: base 8c7fa15 = the 0034 CI's
  nativemetal-source.tar.gz, a73f22c = 0016), harness c432999 on codex/hybrid-lane
  (`patches/nativemetal-fuxi-0016-split-commits.patch`, workflow cp line), frontend CI **43663211587**:
  - Bridge: `NmbCommandBuffer` = Mutex{current runtime CommandBuffer, earlier parts, counted} + its queue's `QueueOpen`
    {open count, overlapped}. `nmb_command_buffer_split`: commits `current` and continues in a new runtime command buffer
    of the same queue, only while the queue has exactly this one open command buffer and never had two at once (so no
    other command buffer can commit between its parts: Metal's commit order holds). Handlers/scheduled wait go to the
    last part (registered at the final commit); wait waits all parts; status = Error if a part failed, else the last
    part's; error = first description; GPU start = first part's, end = last part's; new `nmb_command_buffer_gpu_busy` =
    parts' GPU times added up; `nmb_command_buffer_earlier_uses` / `_wait_earlier` for the early parts. The Mutex is never
    held across a runtime call (a scheduled handler may ask for the status on the same thread).
  - Frontend: `fuxisplit=<us>` read once from kern.bootargs (absent = never splits). Before each encoder opens
    (render/compute/blit), if the current part already holds an encoder, has encoded >= threshold us and fewer than 3
    parts went early: flushSurfaces(_surfaces) as at commit, then split. Split command buffers sit in `gSplitOpen` until
    commit; `waitForResource` also waits for early parts that use the resource; the completed hook's GPU time is now
    `gpu_busy` (= end - start where nothing split). Logs `FUXI SPLIT: parts= command_buffers= refused= failed=`.
  - Not compiled locally (the runtime has unix-only modules; no other Rust target installed); rustfmt parses both files.
- Loaders: `native-hyb4-p1-split-loader` (hyb4-p1 with sweetprof=90=>fuxisplit=1500, args 602, boot.img 66bf09c4...),
  `native-tour-hyb4-split-loader` (tour-hyb4 with fuxivsynctoff=6000=>fuxisplit=1500, args 595, boot.img 593ef3f1...),
  both re-based and verified.
- Running: `hybrid-driver\chain-0035.sh` = CI 43663211587 -> artifact (digest) -> wait for vt-ab-0034b -> pull 20261115i
  -> build-0035 (phone + 0016 bridge/frontend; only the bridge dylib, FuxiNativeMetal, tarballs and log may differ from
  0034's artifact) -> deploy -> boots: hyb4 (no split, 0035 baseline) 420 s, hyb4+split 420 s, tour+split shots.
- **vsync offset A/B on root 0034 (fuxihyb=4, @3, 420 s each)**:
  | offset | backboardd Hz | CPU ms/frame | gpu_ms | single flips | flog TE gaps >= 2 (producer / copy late) |
  |---|---|---|---|---|---|
  | 6000 (`native-layout0034-hyb4-vt6-boot-1`) | 86.4 | 5.01 | 4.31 | 84 % | 19 (16 / 3) |
  | 4000 (`native-layout0034-hyb4-vt4-boot-1`) | 86.5 | 5.20 | 4.26 | 81 % | 14 (14 / 0) |
  | 2000 (`native-layout0034-hyb4-vt2-boot-1`) | invalid: **Vulkan device lost at ~85 s** | | | | |
  A bigger heartbeat-to-deadline budget removes the copy-late gaps but not the producer-late ones, so latency is not
  what holds the script back. The flog's 128 frames (Settings flings) already run at ~120 Hz (swap interval mean 8.3 ms;
  the next swap never comes before the previous copy is done, 0/117, but that follows from CPU 5 ms > GPU 4.5 ms).
  Flip gaps of the 6000 boot: 18635 single, 2318 two-period, 197 three, 1049 longer (mostly the script's still pauses):
  the real misses are the ~10 % two-period gaps. Per 10 s window (FUXI MTL) the slow windows (57-73 Hz) have the GPU
  only 26-30 % busy, and census windows show m2v's per-frame cost about the same in slow and fast windows; the display
  thread's off-CPU time is 98 % the run loop's mach_msg (waiting for the vsync source), not fences: in those phases CA
  has nothing new to draw on some vsyncs (app side), not a display-thread overrun.
  The 2000 boot: `command buffer completed with an error and no description (last: GPU time: DeviceLost)` at line 3672,
  then every command buffer/texture refused (DeviceLost) and the gate fell back to CPU (`runtime-failure next-respawn=CPU
  persisted=1`); no kernel GPU fault/hang/timeout line around it (ABI stuck 0, timeouts 0). First device loss in the
  0030-0034 boots; cause unknown; offset 2000 is not used again.
- Frontend CI 43663211587 **success** (nativemetal 0016 compiles); artifact digest a703de86... matched; chain-0035
  continues (pull 20261115i, build, deploy, boots).
- **Pull 20261115i** (09:44-09:51): phone after the vt boots = sha256 `1be16a690c86b17717ee6d1c854a3d7dced39acbe929cb379efe92c5c99ab46a`
  (560 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115i`; 20261115h deleted as superseded, index updated).
- 0035 built: `images-build\native-layout0035-root.img` sha256 `1f2f29845a08befdbdacde696d333b2d17d02b9dc55a63ed10a8e18f3c2053f3`
  (phone 1be16a69... + CI 43663211587: changed vs 0034 only the bridge dylib, FuxiNativeMetal, the source tarballs and
  the build log; FUXI SPLIT in the frontend).
- **nativemetal patch 0017 (frame diagnostics, report only)**: `nmb` 2nd commit, harness 8385ead, frontend CI
  **43665766682**. Per display frame (a commit that stored an IOSurface): encode time = command buffer creation ->
  commit, gap since the previous frame's commit in 8270 us periods, and what the committing thread made since its
  previous frame (thread_local counts and time inside: textures, buffers, pipelines, surface imports). Every 600
  frames: `FUXI FRAME: n= encode_ms <2/<4/<6/<8/<10/<13/<17/more ... gap_periods 1/2/3/4+/idle ...` and
  `FUXI FRAME: heavy|light n= encode_us= draws= passes= make_us= with_tex= with_buf= with_pipe= with_import=` (heavy >= 6
  ms, light < 4 ms). Purpose: tell whether the ~10 % two-period gaps are display-thread overruns, and what the heavy
  frames do (resource making vs draws), since the census shows no per-frame m2v cost difference between slow and fast
  windows. 0016 regenerated byte-identical.
- 0035 deployed 09:58-10:06 (19 blocks, READBACK_MATCH): **phone root is now 0035**.
- **metal2vulkan patch 0019 (census `passes` line)** (`m2vb` 7e05683, 0018 regenerated byte-identical; harness 7854f58;
  frontend CI **43666120640** = nativemetal 0016+0017 + metal2vulkan 0019; the 0017-only run 43665766682 was cancelled):
  per window the ended render encoders' enc, inst (instances), tile reach none/every/draws, DontCare loads answered and
  encoders bounded to drawn tiles, reach clears (each a render pass of its own), later depth/stencil resolve passes,
  resolving instances, passes recorded later, colors' final loads clear/load/dontcare, colors larger than the render
  area. Why: IBPROF shows ~32 render passes per GPU job against ~11 render encoders per frame. `hyb-census.py` prints it
  ("render encoder passes"). Local `cargo check` (with the temporary Windows stub, reverted) passes.
- Scripts ready for 0036: `pull-phone-20261115j.sh`, `build-0036.sh`, `deploy-layout0036.sh` (chain after the 0035 boots).
- **0035 boots** (same loader args as 0034 vt6 = vsync offset 6000; `script-metric.py` now takes `--loops N`):
  - `native-layout0035-hyb4-p1-boot-1` (no split): the host USB monitor died at +165 s (`USBError Entity not found`;
    the phone kept running and came back to Android by itself), so only 7 loops: 81.3 Hz, 79 % single flips, governor
    max 70.2 C. 0034 vt6's first 7 loops: 86.8 Hz, 84 %, 62.3 C. Its flog: the two-period gaps are producer-late with
    the next swap ~11.7 ms after the previous START (CPU encode then ~5 ms of GPU before the copy's fence, serial).
  - `native-layout0035-hyb4-split-boot-1` (fuxisplit=1500): 20 loops 84.7 Hz, 80 % single, gpu_ms 4.25, governor max
    73.0 C (vt6 86.4 Hz 84 %, vt4 86.5 Hz 81 %). `FUXI SPLIT: parts=36000 command_buffers=18005 refused=0 failed=0`
    (about one early part per display command buffer), no bad lines. Per-loop Hz spans 57-94 in every boot, so one
    boot's mean is +-2 Hz: **Hz neutral**. flog: copy_submit->fence mean 2289 us (vt6 4284), swap->start 4676 us (vt6
    7060): **~2.4 ms less display latency**. The gaps stay producer-late (18 of 21), now with the next swap 6.2-10.6 ms
    after the previous START: the frames that miss are late by 1-2 ms, the CA frame's CPU time itself.
  - `native-layout0035-tour-split-boot-1` (tour + hyb4 + fuxisplit=1500): the host USB monitor died again after shot 4
    (`USBError Entity not found`, as in the 0034 tour at shot 11; the phone came back after the reset). Shots lock,
    home, app-library, app-library-idle. **Pixel diff vs `native-layout0034-tour-hyb4-boot-1`**: home differs only in
    the battery glyph (x 927-968, y 32-69); lock and app-library only in the clock/battery and rows 1591-1608, the small
    moving glyph that 0034's own second reads already flag (its shots 4 and 8, x moves 505 -> 520). **Split renders the
    same.**
  - Verdict: keep fuxisplit as a latency option (Hz neutral, ~2.4 ms less to scanout, no failures); the jank left is
    the CA frame's own CPU time. 0036 (frame diagnostics + passes census) tells where it goes.
- Host USB monitor deaths (`Entity not found` mid-run) now hit 2 of the last 4 boots; earlier logs show the same error
  only at the deliberate end reset. Host-side (the phone keeps running); watch for it, reseat the cable if it repeats.
- User (mid-turn): the small black dot (rows 1591-1608) was there from the start, not the hybrid lane: confirmed.
- Governor boost vs Hz (split + 0034 vt6 + vt4 boots, each 10 s `FUXI PERF: stats` paired with the window before it):
  boosted 2.5-5 s of 10: 83.6 Hz (23 windows, 66.6 C); 5-7.5 s: 86.2 Hz (29, 64.8 C); 7.5-10 s: 85.8 Hz (32, 62.4 C).
  "high" is gold 2323 / prime 2342 MHz, "cool" 1402 / 1363 MHz: a 1.7x clock swing moves Hz by ~2, so the late frames
  are not plainly CPU-clock bound (or are bound in phases the boost does not cover). Logical CPU 6 is the X3 and is in
  fuxicores=0x7c (cpu1 hosts the USB monitor), so no core is left out.
- **Pull 20261115j** (10:46-10:53): phone after the 0035 boots = sha256 `068e9ee3c3f409af6ff4cc59f466a67e08edfe5bd125888d530ccc12bba381bb`
  (574 blocks vs 0025 in `fuxi_Backup\ios-root-phone-20261115j`; 20261115i deleted as superseded, README index updated).
  `chain-0036.sh` continues: build-0036 -> deploy -> hyb3 census, hyb4 (frame diagnostics), hyb4 + SpringBoard sweetprof.
- `hybrid-driver\tools\frame-summary.py <evidence> [--blocks]`: sums the `FUXI FRAME` blocks (encode-time and
  commit-gap distributions, heavy vs light frames).
- **0036 deployed** (chain-0036, 11:10): phone before = `068e9ee3...` (pull 20261115j), 0036 root `c1db260b...0d5f`,
  19 blocks, READBACK_MATCH. Boots:
  - `native-layout0036-hyb3-p1-boot-1` (census, lane random per 10 s): 77.5 Hz, 68 % single flips, governor max 73.4 C.
    T1: 35.1 us/draw (lane on 32.0 vs off 36.4, same-pipeline -15.6 %), between draws 22.2 us; encoders 604/s open 24.8
    end 75.9 us (p9 begin/execute/end render pass 18.7, p7 steps before the pass incl. reach clears 12.9, p13 9.9).
    Passes line: 1.00 instance per encoder, 0.39 reach clear passes per encoder, colors 2.00 per encoder, a color larger
    than the render area in 221288 of 226579 encoders, load clear 238938 / load 214220 / dontcare 0, no later resolves.
  - `native-layout0036-hyb4-p1-boot-1`: not booted (PowerShell `SetConsoleWindowTitle` 0xE9 at boot-exp.ps1:17, the
    adb map step, before fastboot); rerun as `native-layout0036-hyb4-p1-boot-2` by `hybrid-driver\rerun-0036-hyb4.sh`.
  - `native-layout0036-hyb4-sbprof-boot-1` (hyb4 + sweetprof=10090 on SpringBoard): 86.4 Hz, 80 % single, gpu_ms 4.27,
    70.6 C. SpringBoard main thread t243 93 % waiting in its run loop, every other thread >= 98 % waiting: SpringBoard is
    not the limit (same as the 0024 profile). `frame-summary.py`: heavy frames (n 5660) encode 7964 us, 132.7 draws,
    15.1 passes, make 187 us, 30 % make a texture; light frames (n 10249) 2413 us, 60.8 draws, 7.2 passes. Encode-time
    distribution: <8 ms 92.8 %, 8-10 3.7 %, 10+ 3.6 %. The heavy frames overrun the 8.33 ms budget by themselves.
- **metal2vulkan patch 0020 (strip reach clears, `fuxihyb=5` lane without census, `6` lane + census)** (`m2vb` 9fd2ddc,
  0019 regenerated byte-identical by `git diff`; harness 92285a0; frontend CI **43672582848**): a Metal `Clear` load
  clears the render area rounded up to 32 px tiles (the reach); m2v emulated it with its own render pass over the whole
  reach (vkCmdClearAttachments, full barriers) and then LOADed in the pass. Draw scissors are clamped inside the Metal
  render area (render_state.rs), so with 5/6 a stored color (not bounded DontCare tiles, not left out, no sample planes,
  no stand-in, no tile dispatch in the pass) clears only the strips past the render area (right strip over the reach's
  height, bottom strip over the area's width), each in a render pass over the strip alone, and the main pass keeps its
  CLEAR. Expected gain is GPU-side (no full-reach GMEM clear+store and no GMEM load of the area); CPU may cost a little
  more (1-2 small passes instead of 1). Depth/stencil keeps the old path. Census `passes` line gains strip, rkpx, skpx
  (reach and strip pixels / 1024); `hyb-census.py` prints them. Local cargo check passes (temporary stub reverted).
- Loaders (from native-hyb4-p1-loader / native-tour-hyb4-loader by prepare-args-on-loader.py, re-based source/swaps,
  originals `*.vs-hyb4`): `native-hyb5-p1-loader` (boot.img 3178d958...), `native-hyb6-p1-loader` (a20d673a...),
  `native-tour-hyb5-loader` (470bae3c...).
- `hybrid-driver\chain-0037.sh` (running): after rerun-0036 -> CI 43672582848 + digest -> pull 20261115k (then deletes the
  superseded 20261115j delta) -> build-0037 (bridge must change, frontend may) -> deploy -> tour+hyb5 shots, hyb5 420 s,
  hyb4 420 s, hyb6 census 420 s. Its boot() retries once when a boot fails before fastboot (no `boot_time=`).
- `native-layout0036-hyb4-p1-boot-2` (rerun, hyb4 = lane without census, the 0036 baseline): 85.0 Hz, 78 % single flips,
  gpu_ms 4.28, governor max 71.4 C, bad lines 0. `frame-summary.py`: heavy frames (n 6026) encode 8098 us, 133.5 draws,
  14.8 passes, make 361 us; light (n 9657) 2462 us, 62.1 draws, 7.3 passes; encode <8 ms 92.2 %. Same picture as the
  sbprof boot (86.4 Hz): Hz within the +-2 single-boot noise of 0034/0035.
- **chain-0037** (11:48-): CI 43672582848 success, zip digest matched (476ac255...). Pull 20261115k 11:49-11:57: phone
  after the 0036 boots = `c8f6519542b660e1d3a630faa194f76a6a4defb2c445363bae7926befc2f70b1`; 20261115j delta deleted.
  0037 root `ed0dd50339b749974fc2ccc00c9184be7a617627445a24a04fcd01729c6354df`, deploy 19 blocks 76 MiB, write 61 s,
  READBACK_MATCH 12:07 -> **phone root is 0037**.
  - `native-layout0037-tour-hyb5-boot-1`: the host USB monitor died at boot.log line 298 (early launchd, `USBError
    Entity not found`), no shots; host-side, not the lane (the same 43 trust-cache lines as any boot). Rerun after the
    chain by `hybrid-driver\rerun-0037-tour.sh` (`native-layout0037-tour-hyb5-boot-2`).
  - `native-layout0037-hyb5-p1-boot-1` (lane + strip reach clears): **86.1 Hz**, 78 % single, gpu_ms 4.28, 70.2 C;
    heavy frames (n 6534) encode 7928 us, 132.6 draws, 14.7 passes.
  - `native-layout0037-hyb4-p1-boot-1` (baseline; try 1 failed with `no fastboot device` before boot_time, the chain's
    retry booted it): **86.9 Hz**, 80 % single, gpu_ms 4.25, 69.8 C; heavy (n 5474) 7943 us, 132.7 draws, 15.1 passes.
  - `native-layout0037-hyb6-p1-boot-1` (strip + census): 83.6 Hz (census cost). T1 31.4 us/draw, between draws 20.0;
    encoders 655/s open 18.9 end 68.7 us (p7 14.1 vs 12.9 in 0036 hyb3: the strip passes cost a little CPU).
    **reach clears strips-only 95331 of 95331**, but reach px only ~4.0K per encoder (~10K px per reach clear), strip px
    1.2K per encoder (30.9 % of the reach px). The reach clears are on small surfaces, so the GMEM saving is tiny.
  - **Verdict 0020: neutral** (86.1 vs 86.9 Hz within +-2, gpu_ms unchanged). It stays behind fuxihyb=5/6; daily stays
    fuxihyb=4. The heavy frames (~133 draws, ~15 passes, ~7.9 ms encode) remain the limit: per heavy frame ~133 x
    (31 us m2v + 20 us between draws) + 15 x ~88 us encoder open/end.
- `native-layout0037-tour-hyb5-boot-2` (rerun): the host USB monitor died again at shot 4 (`Entity not found`,
  monitor_exit=1; 0035 stopped at shot 4 too). Shots 1-3 (lock, home, app library) match 0034 (mean diff 0.00-0.01);
  the `-b` shots differ by 3.8-4.7 mean (normal for animated content; 0035 had 6.57/5.79). The strip clears render right.
- **Display-thread profile, 0037 hyb4** (`native-layout0037-hyb4-p1-boot-1`, t520, 100 Hz sweetprof, live 13737 /
  wait 15655 samples). New scratch tools: `stacksym.py` (symbolized PROF stacks per tid with --image) and `disfn.py`
  (capstone disassembly of a Mach-O symbol). Self time (prof-pc top buckets, 5533 samples):
  memmove 874 (tu_CmdBindPipeline >=226, ~1.4 us per bind); kernel trap bucket 670 (Turnip submit ioctl 259, ~100 us
  per submit; kern SwapWait 159; SwapEnd 86); os_unfair_lock 241 (malloc 162); pthread mutex lock/unlock 289;
  malloc family ~450 (tcache 100); `LastRetained::retain_draw` 233 (a hot Arc decrement: Rust aarch64-apple-ios builds
  atomics as LL/SC ldxr/stxr, no LSE, ~0.4 us per draw; system libs use the `$VARIANT$armv81` LSE variants, so LSE
  works at EL0 -> `+lse` is a candidate bridge flag; never target-cpu=cortex-x3, XNU leaves SVE off); chkstk 91,
  memset 88, objc_msgSend 126, SipHash 56. Inclusive: CA's own CPU is small (its blur is mostly draws); m2v-related
  work is ~49 % of display-thread live time. The work is serial: CA builds a layer tree, the frontend calls nmb, m2v
  records Vulkan, all on t520.
- **Next (m2v unchanged, nativemetal patch 0018 "deferred encoding", `fuxihyb=7`)**: a bridge worker thread runs m2v for
  render encoders so CA/frontend and m2v overlap. backboardd only. Render setters, draws and encoder end are queued in
  one global FIFO (owned clones / copied bytes) and return true at once; every other nmb call (guard) first waits until
  the worker has processed this thread's last queued item (no-op on the worker and for threads without queued work), so
  the order the frontend sees never changes. Read-only queries that cannot depend on queued work (buffer contents,
  length, gpu address, last error) skip the wait. Worker errors are logged; a refused draw becomes a commit-time error.
  `FUXI WORKER` stats every 10 s (queued, drained by reason, wait us). 7 = lane on, census off, no strips (m2v treats
  v>=2 as lane on). Expected: heavy-frame encode -35-40 % if the frontend/CA half and the m2v half are similar.
  Later candidates: +lse, Turnip bind-pipeline memmove, retain dedup, submit cost, encoder-end fast path.
- **0018 implemented** (nmb/native commit `2fb5717` on 0017 `ce34e6f`; new `bridge/FuxiDeferred.rs`, NativeMetalObjects.rs
  edits, `mod fuxi_deferred` in NativeMetalBridge.rs; patch `patches/nativemetal-fuxi-0018-deferred-encoding.patch` =
  `git diff ce34e6f..HEAD`; harness codex/hybrid-lane `d5061c3` adds the cp in native-metal-check.yml; frontend CI
  **43681679209**). Gate: `fuxihyb=7` in kern.bootargs AND getprogname()==backboardd AND the thread name contains
  `coreanimation.display` (decided once per thread). The deferred encoder is `NmbEncoder::Deferred(SlotPtr)`; its
  open/setters/draws/fence/end/discard push `Op`s (owned clones, set*Bytes copied, <=128 B inline) to one global FIFO
  and return true. `guard` = `drain(Location::caller())` + old body: a thread with queued work spins 40 us then parks
  until the worker's `done` reaches its last sequence number; the worker's own MINE is 0 so callbacks there never wait.
  `guard_free` (no drain) for buffer new/contents/length/gpu address, texture new/describe, sampler new. A refused
  deferred call records the first reason on its command buffer (`deferred` Failure): commit returns it (frontend runs its
  refusal path), split answers 0. Worker "fuxi.deferred-render" (8 MiB stack) copies the first display thread's QoS,
  sched param and time-constraint policy; it spins 50 us before parking. Logs: `FUXI WORKER gate|thread|start|fail` and
  every 10 s `FUXI WORKER s= ops= open= draw= end= busy= batch=avg/max lag= park= fail= wait=n/us top=line:n/us,...`
  (line = call site in the patched NativeMetalObjects.rs). Parser: `hybrid-driver\tools\worker-stats.py <evidence>`.
- Loaders (from the hyb4 ones by `prepare-args-on-loader.py ... "fuxihyb=4=>fuxihyb=7"`, re-based, originals
  `*.vs-hyb4`): `native-hyb7-p1-loader` (boot.img b14b278a...), `native-tour-hyb7-loader` (c9a50320...).
- `hybrid-driver\chain-0038.sh` (started 13:40): CI 43681679209 + digest -> pull 20261115l (then deletes 20261115k) ->
  backup index row (`update-backup-index.py`, rewritten with Write after a heredoc ate its backslashes and split the
  README row; repaired) -> build-0038 (bridge+frontend only) -> deploy -> tour+hyb7 shots, hyb7 420 s, hyb4 420 s, hyb7 #2.
- **0039 prepared (+lse)**: the bridge (with the runtime and the translator, one fat-LTO unit) is built for
  aarch64-apple-ios = apple-a7 (ARMv8.0); `hybrid-driver\tools\lse-count.py` on the 0037 bridge: LSE atomics 0, LDXR
  9396, STXR 9393 (every Arc count / lock / queue op is an LL/SC loop). Harness `patches/fuxi-bridge-lse-builder.patch`
  (applied after fuxi-bridge-lto-builder.patch; base blob b9c555b) appends `-C target-feature=+lse` to
  CARGO_TARGET_AARCH64_APPLE_IOS_RUSTFLAGS; harness `975fe4c`, CI **43682227587**. No boot-arg gate (it is a codegen
  change; the cores are ARMv9 and the system libs already run their armv81 LSE variants). 0018 stays (only under hyb7),
  so hyb4 on 0039 measures +lse alone against 0038 hyb4. Scripts: `pull-phone-20261115m.sh` (target = 0038 root, its
  sha read from the .sha256), `build-0039.sh` (fails unless the bridge has >1000 LSE atomics), `deploy-layout0039.sh`,
  `chain-0039.sh` (started; waits for chain-0038's ALL_DONE, then CI -> pull m (deletes l) -> build -> deploy -> hyb4,
  hyb7, hyb4, hyb7 420 s each).

## 2026-11-15 ~14:10-15:10 (continued, user: "好你不要中斷") 0038 results, Turnip bind-pipeline memmove, 0040 prepared
- **chain-0038**: phone root 0038 (deferred encoding 0018 under `fuxihyb=7`).
  - `native-layout0038-tour-hyb7-boot-1`: the host USB monitor died before shot 8 (`USBError Entity not found`,
    monitor_exit=1, the same host-side failure as 0037). Shots 1-7 vs 0034: lock/home/app library/spotlight 0.00-0.02
    mean; `-b` shots 0.15/0.91; shot6 "today" 14.6 mean = the app switcher's Settings card scrolled to a different place
    (user state), both render right. **0018 renders right.**
  - `native-layout0038-hyb7-p1-boot-1`: **87.2 Hz**, 84 % single, gpu_ms 4.35, 72.2 C, late +564 (see caveat).
  - `native-layout0038-hyb4-p1-boot-1` (baseline): **85.3 Hz**, 79 % single, gpu_ms 4.23, 70.6 C, late +23.
  - `native-layout0038-hyb7-p1-boot-2`: **87.8 Hz**, 85 % single, gpu_ms 4.37, 74.6 C, late +566. worker-stats: 858
    encoders/s, 7824 draws/s, worker busy 25.4 %, 6.25 us per op, fail 0; the display thread waits 84/s, mean 2367 us
    = 19.9 % of wall clock, almost all at NativeMetalObjects.rs:1548 (the on_completed drain at commit).
  - Caveats: FUXI FRAME encode_us = committedUs - createdUs, and committedUs is taken BEFORE the on_completed drain, so
    under hyb7 the worker wait is outside encode_us (encode looks shorter than it is). `late` (FuxiMetal) is measured from
    committedUs, so hyb7's late count is inflated by the same drain. CA calls commitAndWaitUntilSubmitted (commit +
    waitUntilScheduled + fuxiWaitStoredSurface) and swap wait is off on these loaders (`fuxiswapwait=0`), so deferring
    the commit gives no cross-frame overlap: the display thread waits ~80 % of the worker's m2v time at on_completed.
  - **Verdict 0018: neutral to slightly positive** (hyb7 87.2/87.8 vs hyb4 85.3 Hz, +5-6 % single, at the edge of the
    +-2 single-boot noise; runs 2-4 C warmer). Heavy frames
    (~132 draws, ~15 passes, ~8 ms) stay the limit.
- **Worker / display profile (0038 hyb7)**: m2v self time on the worker: the largest single item is memmove under
  `tu_CmdBindPipeline` (~14 % of m2v time). Turnip (Mesa 85fda3b + UniXNU fb6aad6a + ios-fuxi f44bf569) does
  `cmd->state.program = pipeline->program;` = a 0x2770 (10096-byte) struct copy on EVERY vkCmdBindPipeline. Others for
  later: draw_resident memmove, nmb_render_encoder_new memmove, Rust Mutex (pthread) ~8 %, os_unfair_lock ~6 %, malloc
  ~8.6 %, retain_draw ~6 % (+lse in 0039 should help).
- **P2 step 14: hybrid Turnip** (harness codex/hybrid-lane `e4600e8` -> `01bb200` -> `17ada77`):
  - `patches/mesa-hyb-program-pointer.patch` (made in `work\mesa-src`, a sparse Mesa 85fda3b clone with UniXNU
    turnip-graphics-cache-feedback + turnip-empty-scissor as local commit 527f7da, core.autocrlf=false/eol=lf, by
    `hybrid-driver\turnip\make-program-pointer.py`): `struct tu_cmd_state::program` becomes `struct tu_program_state *`
    (`= &pipeline->program`), 11 uses in tu_cmd_buffer.cc and 3 + the patch_control_points argument in tu_pipeline.cc
    become `->`. First CI (43687448181) failed: trace_start_draw takes the stage hashes as `char *` (tu_sha1_str), so the
    pointer must not be const; fixed in 17ada77. Only pipelines emit program state in this Mesa (no shader-object
    writer), and every reader is on the draw path, which needs a bound pipeline. Turnip CI **43688079557**
    (artifact `fuxi-turnip-hybrid`, out/ci/mesa/libvulkan_freedreno.dylib).
  - **nativemetal patch 0019** (nmb/native `509816e` -> `28075fc`): the front end opens
    `libvulkan_freedreno_hyb.dylib` (same directory as the default loader) instead of `libvulkan_freedreno.dylib` when
    the boot line's `fuxitudebug=` list has the flag `hyb` (e.g. `fuxitudebug=nocb,hyb`); missing file -> default
    loader, logs `FUXI TURNIP: loader=...`. First version used `fuxiturnip=1`, but the p1 boot line is 600 bytes and
    XNU's limit is 608 (prepare-args-on-loader asserts < 608): any `fuxi*=1` token is >= 8 bytes and every existing
    token is in use (checked against the kernel source and the 0038 source tarballs; `sweetlog` defaults to ON, fuxihyb
    is parsed as an integer by m2v). Turnip ignores the unknown TU_DEBUG flag. Frontend CI **43687954922** (e4600e8's
    43687454300 cancelled).
  - Loaders (`prepare-args-on-loader.py ... "fuxitudebug=nocb=>fuxitudebug=nocb,hyb"`, re-based, originals `*.vs-hyb4`):
    `native-hyb4t-p1-loader` (604 bytes, boot.img 12caee6a...), `native-tour-hyb4t-loader` (603, 9cef25d7...).
  - Root 0040 = phone after the 0039 boots + 0019 front end + `/usr/local/lib/libvulkan_freedreno_hyb.dylib` (0755
    root:wheel; staged by `hybrid-driver\stage-test-hybturnip.sh` = phonebase staging + the copy, sha filled in by
    build-0040). The default Turnip (7467...) is unchanged, so boots without `hyb` equal 0039. Note: the Turnip rebuild
    is not bit-reproducible (04ae43c0 vs c97ac49a vs deployed 7467...), so hyb4t vs hyb4 = rebuilt+patch vs deployed.
  - Scripts: `pull-phone-20261115n.sh` (target 0039), `build-0040.sh <frontend run> <turnip run>` (digest-checked
    downloads; frontend must change, bridge/tarballs/log may; bridge must keep >1000 LSE atomics; hyb dylib must differ
    from 7467... and the Turnip run log must name mesa-hyb-program-pointer), `deploy-layout0040.sh`, `chain-0040.sh`
    (started 15:06; waits for chain-0039's ALL_DONE, both CI runs -> pull n (deletes m) -> build -> deploy -> tour+hyb4t
    shots, hyb4t, hyb4, hyb4t, hyb4 420 s each).
- **15:30 handover**: the "續 001" session hit its 5-hour limit at 15:26 (resets 16:10); a new session ("接續001",
  915c4d1a) took over. Frontend CI 43687954922 = success (01bb200). chain-0040 was still waiting for chain-0039, so it
  was killed and restarted unchanged from the new session (same script, log chain-0040.log); stale `tail -F` monitors of
  the old session were killed. chain-0039 (old session's background task) keeps running (hyb4 boot-1 from 15:26).
- **Bind memmove check settled**: deployed Turnip (0025 share, 7467...) `tu_CmdBindPipeline` @0x56498 has
  `+0x238 mov w2,#0x2770 ; bl <memcpy stub>` (the 10096-byte program-state copy); the hybrid Turnip from 43688079557
  (`tu_CmdBindPipeline` @0x56128, first 0x1000 bytes) has no 0x2770 and no copy call there. (disfn's default length
  0x200 was too short; stub targets show as `_driParseConfigFiles.cold.4+...` because stubs follow the last symbol.)

## 2026-11-15 ~15:30-16:25 (session "接續001") 0039 results, a DeviceLost under hyb7, IOMFB kernel cost, overlap idea
- **chain-0039** (phone root 0039 = +lse bridge/runtime/translator):
  - `native-layout0039-hyb4-p1-boot-1`: **84.4 Hz**, 79 % single, gpu_ms 4.29, 71.4 C, late +16.
  - `native-layout0039-hyb7-p1-boot-1`: **DeviceLost** about 2 min in (boot.log stops growing at 15:43, script never
    finished). At the p1 script's edge swipe (540,2390 -> 540,999), right after backboardd created two 1125x2436
    surfaces (SWEET SURF ids 15/16) and `com.apple.coreanimation.asynchronous` made its first bridge call (FUXI WORKER
    thread ... deferred=0), a command buffer "completed with an error and no description" and then every command
    buffer / texture was refused `DeviceLost` (18678 lines); the FuxiMetal gate latched runtime-failure (next respawn
    CPU; the gate resets on a new boot, so later boots are unaffected). Kernel side clean: FUXI GPU ABI submits all ok,
    fence timeouts 0, stuck 0, no fault line; Turnip's own reason is not in the log. The worker reported fail 0. In m2v
    a lost device comes from a Turnip wait / counter read returning VK_ERROR_DEVICE_LOST (command_buffer/completion.rs
    `failure()`); Turnip's KGSL backend sets lost on a failed submit ioctl or a GPU_RESET_STAT other than no-error.
    The same step passed in 0038 tour-hyb7 + 2 x hyb7 and in 0039 hyb7 boot-2: **intermittent, 1 of 5 hyb7 boots**.
  - `native-layout0039-hyb4-p1-boot-2`: **86.3 Hz**, 80 % single, gpu_ms 4.23, 73.4 C, late +4.
  - `native-layout0039-hyb7-p1-boot-2`: **86.4 Hz**, 85 % single, gpu_ms 4.35, 74.9 C, late +563. worker-stats: 842
    encoders/s, 7609 draws/s, worker busy 25.1 %, 6.33 us per op, fail 0; display thread waits 83/s, mean 2385 us =
    19.7 % of wall clock, almost all at NativeMetalObjects.rs:1548 (on_completed drain at commit).
  - **Verdict +lse: neutral** (hyb4 84.4/86.3 vs 0038's 85.3; hyb7 86.4 vs 87.2/87.8). retain_draw samples -25 % on
    the display thread, no Hz change. Kept: 0040 is built on it.
  - chain-0039's wrapper shell belonged to the old session and never wrote `chain exit=`; it was appended by hand at
    16:21 (the script had reached ALL_DONE and exited) so chain-0040 could go on.
- **Display-thread profile** (0039 hyb4 boot-1, t513, 5191 live samples): 10.8 % is `_mach_msg2_trap`. Live samples
  (x16 = 0x7fffffff, sweetprof's IPI sample) with the user pc at the trap = the thread on a core inside the kernel. Of
  the trap's stack samples, IOMFB `kern_SwapBegin` 131 and `kern_SwapEnd` 78. SwapBegin's handler is only
  lock + increment (SweetFramebuffer.cpp `externalMethod`), so its >= ~51 us per call is kernel entry / IOKit dispatch
  / fLock wait (vsyncFired holds fLock while it notifies every client; presentNext holds it across present_begin).
  FUXI GPU ABI per call: status 21.5 us, submit 71.9 us (backend 66: idle 23, frame 20, ring 9.6). Candidate: let
  sweetprof's IPI handler also keep the kernel pc when the core was in the kernel, to see where kernel time goes.
  All 155 memmove stack samples are at tu_CmdBindPipeline+0x240 (0040's target).
- **Next lever, idea for P2 step 15: overlap across frames under hyb7.** The display thread waits at commit (the
  on_completed drain, ~2.4 ms a heavy frame) because CA swaps right after commitAndWaitUntilSubmitted and only ring
  order keeps the kernel's present copy behind the client's submit (swap wait is off). The kernel already has a pending
  producer: `IOSurfaceLock(kIOSurfaceLockAvoidSync)` -> `sweet_gpu_writer(s, true, pending)` counts a writer without
  waiting (present_begin refuses while gpuWriters != 0; pending-to-active stays in the same writer epoch). FuxiMetal's
  Frontend.mm uses it for earlySubmit (`_pendingLease`, `lockScanoutPendingSurface`) and takes the real lease when it
  executes. NativeMetal takes no leases. Plan: under a new fuxihyb value, NativeMetal's commit of a deferred
  display-thread command buffer that stores IOSurfaces takes pending leases on them, queues on_completed /
  on_scheduled / commit to the worker and returns; waitUntilScheduled returns at once for it; the scheduled handler
  (submit in the ring) drops the leases. Heavy frame estimate: display thread ~3 ms + worker ~5 ms in parallel instead
  of ~8 ms in series. First rule the hyb7 DeviceLost in or out.

## 2026-11-15 ~16:30-17:00 (session "接續001") P2 step 15: overlapped commits, nativemetal patch 0020, fuxihyb=8
- **Patch 0020** (`nativemetal-fuxi-0020-overlap-commits.patch`, nmb/native d1de600, harness codex/hybrid-lane 50caa82,
  frontend CI 43695587481). `fuxihyb=8` = everything `fuxihyb=7` does, plus:
  - FuxiDeferred.rs: `process_mode()` (7 or 8, backboardd only; `process_on()` = mode != 0), `overlap()` = mode 8 and
    an eligible (deferred) thread; `Op::Task(Box<dyn FnOnce() + Send>)` queued with no slot via `task()` (same queue,
    same MINE seq, so this thread's next guard() call drains it); worker stats add `task=`.
  - NativeMetalObjects.rs/.h: `nmb_command_buffer_commit_deferrable()` (guard_free) and
    `nmb_command_buffer_commit_deferred(cb, completed, scheduled, after, context)` -> 1 queued / 0 not this thread /
    -1 refused. The task does add_completed_handler, add_scheduled_handler, take_failure, commit, uncount (same order as
    the direct path), then `after(context, why)` (why NULL on success).
  - Frontend: in `commit`, after the completion's `__bridge_retained` context, `fuxiCommitOverlapped:` takes a pending
    writer lease (`IOSurfaceLock(s, kIOSurfaceLockAvoidSync)`, retained) on every distinct IOSurface in `_surfaces`
    (all-or-none), sets `fuxiOverlapped` = no scheduled handlers and no blit/compute encoder (`fuxiOtherWork`),
    `fuxiOverlapPending`, CFRetains context for the worker and queues. `commitAndWaitUntilSubmitted` skips
    `waitUntilScheduled` when overlapped. Leases drop (`IOSurfaceUnlock(s, 0)`) in didSchedule (= submitted to the
    ring, so the kernel's present blit is behind it), finish and dealloc. `status` answers Committed while pending.
    `overlapCommitted` (worker): on refusal refuse/failedLocally/drop leases/finish async and release the completion's
    reference; always releases the worker's. Report `FUXI OVERLAP: commits= skipped_waits= kept_waits= leases=
    lock_failed= refused= status= lock_us= unlock_us=` at commit 1 and every 1200.
  - Needs `fuxiswapwait=0` (otherwise fuxiWaitStoredSurface's nmb_command_buffer_wait drains and waits for the GPU);
    the p1/tour loaders already have it.
- **Loaders** (prepare-args-on-loader.py, re-based like hyb7 -> `source.txt.vs-hyb7`/`swaps.txt.vs-hyb7`):
  `native-hyb8-p1-loader` (600 bytes, boot 441016b5...), `native-tour-hyb8-loader` (599, 6e9361f4...), and with
  `fuxitudebug=nocb,hyb`: `native-hyb8t-p1-loader` (604), `native-tour-hyb8t-loader` (603).
- **chain-0041** (`chain-0041.sh 43695587481`, started ~16:50): waits for chain-0040's ALL_DONE and the CI -> pull
  20261115o (deletes n) -> `build-0041.sh` (frontend and bridge must both change vs 0040, both must name
  nmb_command_buffer_commit_deferred, frontend FUXI OVERLAP; same hyb Turnip re-staged) -> `deploy-layout0041.sh` ->
  boots read at boot time from `boots-0041.txt` (default: tour-hyb8 shots, hyb8, hyb7, hyb8, hyb7 420 s; edit it to
  hyb8t/hyb4t if 0040 shows the hyb Turnip wins).

## 2026-11-15 ~17:00-17:30 (session "接續001") 0021 error names, cold-start stutter measured, nativemetal patch 0022 (pipeline prewarm)
- **Patch 0021** (`nativemetal-fuxi-0021-error-names.patch`, nmb/native 906a96b, harness 5aee82b, CI 43697223049 success):
  `nmb_command_buffer_error` names a failure the driver relayed nothing for by the runtime's `{why:?}` (part.error()) and
  `: {refusal}` (part.refusal()), so the next hyb7/hyb8 DeviceLost is named. chain-0041 was restarted with this run.
- **0040 tour-hyb4t stop at Spotlight is the host monitor, not the driver**: fuxi-xnu-monitor.py died in the shot-7 framebuffer
  read (`usb.core.USBError: [Errno 2] Entity not found`, 512-byte control transfers, each shot read twice ~16 s). Earlier tours
  also ended early (0035 at shot 4, 0037 hyb5 at 0/3, 0038 hyb7 after shot 7); only 0034 got all 11. Tour boots without
  -TourShots should run to the end.
- **0040 hyb4t-p1-boot-1**: 83.2 Hz, 81% single, gpu_ms 4.29, late +12, max 72.6 C, vs 0039 hyb4 84.4 / 86.3 Hz (79/80%):
  the hybrid Turnip (no tu_program_state memmove) is neutral within +-2 so far; 0040's own hyb4 boots still running.
- **Cold-start stutter (user: first open of App Library, apps, Control Center stutters on every cold boot)**: read backboardd's
  `<cache>/pipelines.log` out of the 0040 image (read-only qcow2 overlay, `hybrid-driver\inspect-share\inspect-pipelines.sh`,
  output decoded by `hybrid-driver\tools\extract-b64.py`; per-boot stats `tools\pipelog-boots.py`). CoreAnimation makes each
  render pipeline lazily on its display thread on the draw that first needs it: every first screen open makes 14-33 in one burst,
  30-75 ms in total, 1.5-3 ms each (some 12-16 ms), although every one hits the m2v/Turnip disk caches. Within a boot a repeat
  creation of the same descriptor costs median 391 us vs 1818 us the first time (the memo shares the live variant). The 0038 tour's
  late-frame jumps sit on those events (app-library +3, today +14, spotlight +10, control-center +6). The last 12 boots made 174
  distinct descriptors in total; a boot right after a new bridge build (cache misses) spends ~3 s. Test impact: p1 A/B stays valid
  (small equal bursts on warm caches; the first boot after a deploy absorbs misses); first opens and tour numbers are affected.
- **Patch 0022** (`nativemetal-fuxi-0022-pipeline-prewarm.patch`, nmb/native fc1d70e, harness 911f8ca, CI 43698287714):
  frontend only. In backboardd (getprogname), unless `fuxitudebug=` has the flag `nopw`:
  - `native_bridge_metal_open` records the journal size before this boot's F lines (`gJournalOpenSize`).
  - The first successful CA render pipeline calls `fuxiPrewarmKick()`: a serial utility-QoS queue
    `org.blu-sharky.native-metal.prewarm` reads the last 4 MiB before that size, parses every `R ... ok=1` line back into an
    NmbRenderPipelineDescriptor (samples/topo/ds, c%u colors, a%u attributes+layouts, v/f functions with name:type:hex
    constants), dedupes by key (everything from ` label=` to the end of the f segment), keeps the earliest t, drops t<1000 ms,
    sorts by t, caps at 400.
  - Each NBLibrary a function is made from is recorded (weak NSHashTable, `fuxiPrewarmLibrary` at the top of both
    newFunctionWithName forms); a pass maps function names to libraries (nmb_library_function_name), makes the functions
    (nmb_function_new / _specialized), `nmb_render_pipeline_new_with_options(gDevice, &d, {0})` straight through the bridge
    (never the ObjC device methods: noteProblem/nbError reach the failure hook, which can latch backboardd's gate), releases
    the functions and KEEPS the state for the process (the memo is content-keyed and holds only Weak refs, so CA's own
    creation of the same descriptor shares the live variant). Entries whose library is not made yet wait; a new library
    schedules another pass (at most 8).
  - Journal `P t= us= ok= label= v= f= [err=]` per creation; stderr `FUXI PREWARM: pass= libraries= keys= unread= made= refused=
    waiting= spent_ms= slowest_ms= wall_ms= kept=` per pass. The prewarm thread's MINE is 0, so the deferred worker never
    makes it wait; memo locks are held only for get/insert.
  - Host replica of the parser on the 0040 journal (as of the last boot's open): 1596 R lines, 174 keys, 0 unread, 14 dropped
    (<1 s), list 160, 13 distinct functions, 153 specialized.
- **Loaders**: `native-hyb8n-p1-loader` (605 bytes) and `native-tour-hyb8n-loader` (604) = hyb8 ones with
  `fuxitudebug=nocb,nopw` (prewarm off), re-based (`source.txt.vs-hyb8`/`swaps.txt.vs-hyb8`).
- **chain-0042** (`chain-0042.sh 43698287714`, started ~17:20): waits for chain-0041's ALL_DONE and the CI -> pull 20261115p
  (deletes o) -> `build-0042.sh` (frontend must change vs 0041, bridge may; patches 0020-0022 in the tarball; FUXI PREWARM in the
  frontend) -> `deploy-layout0042.sh` -> `boots-0042.txt` (tour-hyb8 shots, then tours hyb8n/hyb8/hyb8n/hyb8 without shots,
  then p1 hyb8 and hyb8n). `post-0042.sh` then pulls 20261115q (deletes p) and extracts the journal into
  `hybrid-driver\pipelog-0042\pipelines.log` (compare CA's R-line us at first opens, prewarm on vs off boots).

## 2026-11-15 evening: session resumed after the 5-hour limit (Claude, session e8267fa4)

- **Orphan chain**: two chain-0041 processes were waiting, an orphan on old CI 43695587481 (pid 6084) and the intended one on
  43697223049 (pid 6877); both would have pulled and written the phone. Killed 6084.
- **`chain exit=` lines**: the wrapper shells (`bash chain-00NN.sh ... > log; echo "chain exit=$?" >> log`) died with the old
  session, so nothing would write the `chain exit=` line the next chain waits for. `hybrid-driver\chain-exit-writer.sh <pid> <log>
  ...` waits for each pid and appends `chain exit=<0|1> (inferred by chain-exit-writer)` (0 when the log has ALL_DONE/POST_DONE).
  Running for chain-0040 (3727), chain-0041 (6877), chain-0042 (7324), post-0042 (7328); output `chain-exit-writer.log`.
- **IOMFB analytics (display-thread IPC on every swap)**: from the dyld cache disassembly, IOMobileFramebuffer's kern_SwapEnd counts
  swaps at [fb+0x678]; from the 62nd (0x3e) on it calls core_analytics_send_data(type 0, "com.apple.iomfb.underrun"), which reads
  the properties `decomp_fail`, `downscale_fail` and `underrun` (IORegistryEntrySearchCFProperty, one IPC each). Our AppleCLCD has
  none, so all three fail, send_data returns 0 and the counter is NOT reset: every swap after the 62nd makes 3 failed IPCs on the
  display thread (est. 60-150 us/frame, ~1-2% of 8.33 ms). With the properties present the send runs, SetParameter(0x11) x2
  (IOConnect selector 68) follows and the counter resets (one burst per 62 swaps).
  - kdraws-src branch `iomfb-analytics` 2a0141c (on 6fc377f): AppleCLCD::start publishes `underrun`, `decomp_fail`,
    `downscale_fail` = 0 (OSNumber 32) right after sweet_fb_refresh_publish; externalMethod logs selectors 64..127 once each too
    (`sweet_fb_logged_hi`; >=128 still every call), so selector 68 shows up once instead of flooding. Same-line replacements in the
    mirror `xnu/fuxi-unixnu/SweetFramebuffer.cpp`, `xnu/patches/0106-fuxi-unixnu.patch` and `0046-sweet-iosurface-iomfb.patch`
    (line counts unchanged); `tools/sync-fuxi-unixnu.py --check` PASS.
  - Harness `patches/kernel-iomfb-analytics.patch` (= kdraws-src 6fc377f..2a0141c), applied after kernel-taps-page1 in
    `kernel-bo-slots-check.yml`; harness 3c66b5a (only commit on top of 0b71826 = kernel-page1-artifact), CI 43700916710.
  - Verify by the display-thread profile (mach_msg2_trap / IORegistryEntrySearchCFProperty samples under kern_SwapEnd gone) and the
    FRAME encode times, not by Hz (below the +-2 Hz noise).
- **0040 verdict (17:53, phone root 0040): hybrid Turnip neutral.** p1 (script-metric.py): hyb4t 83.2 / 87.1 Hz (81 / 81 %
  single, gpu_ms 4.29 / 4.27, late +12 / +5, max 72.6 / 75.3 C) vs hyb4 84.9 / 85.9 Hz (78 / 79 %, 4.25 / 4.26, +52 / +6,
  71.4 / 75.3 C); means 85.2 vs 85.4. No DeviceLost in any of the four boots. memmove share dropped (13.0 vs 15.2 %, the
  tu_program_state copy is gone) but Hz does not move: per-bind savings (~1.4 us x binds) are far below the +-2 Hz noise.
  Kept: `libvulkan_freedreno_hyb.dylib` stays in the image, the default stays the stock Turnip; 0041/0042 run hyb8/hyb7 as
  planned. chain-exit-writer wrote chain-0040's `chain exit=0` at 17:53:26; chain-0041 CI_OK 17:54:19, PULL 17:54:39.
- **IOMFB log check** (old kernel, 0040 hyb4-p1-boot-2): `SWEET FB: backboardd sel 68 in 1/8` appears only twice in the whole
  boot (startup), confirming the analytics send never ran (it would have made 2 calls every 62 swaps). Since 2a0141c logs
  64..127 only once, those startup calls would hide the analytics ones, so kdraws-src ac1aed5 logs SetParameter (68) also at
  its call 1, 2, 4, 8, ... with `n68 <count> p0 <first scalar>` (~11 lines per 420 s boot when the path runs). Harness 4acc88e
  (patch regenerated = kdraws-src 6fc377f..ac1aed5, 5094 bytes LF), CI 43701482816 (supersedes 43700916710).
  Note: sweetprof keeps only each thread's top 12 stacks, and the 0039 `kern_SwapEnd` stack (78) is the swap's own
  IOConnectCallMethod, so the 3 property IPCs never reached the top stacks; `hybrid-driver\tools\swap-ipc.py` counts the
  swap-path stacks per boot anyway. stacksym.py / callers.py / disfn.py copied from the old scratchpad into
  `hybrid-driver\tools\`.
- **Kernel artifact `kernel-iomfb-artifact`** (CI 43701482816, SHA256SUMS OK, kernel f397e255...; contains `n68 %u p0` and
  `downscale_fail`; cmdline.txt same as page1). Loaders, two layers like hyb8: `native-iomfb-spin-loader` =
  prepare-kernel-variant(native-page1-spin-loader, kernel-iomfb-artifact), then `native-hyb8k-p1-loader` =
  prepare-args-on-loader(native-iomfb-spin-loader, hyb8's swaps.txt) -> args identical to native-hyb8-p1-loader (600),
  payload b5d9ce2b..., boot.img 000659b1...; verify-args-on-loader-payload.py OK (boot-exp runs that one, and it requires
  the loader's kernel = its source's, hence the two layers; verify-kernel-variant-payload.py still has an old guard against
  fuxiperf=/fuxiearly= that every current p1 loader carries). Appended to `boots-0042.txt`: hyb8k-p1-boot-1,
  hyb8-p1-boot-2, hyb8k-p1-boot-2 (kernel-iomfb-artifact for the k boots).
- **0041 first results (hyb8, phone root 0041).** tour-hyb8 (18:13-18:27): all 14 shots, rendering correct (control center
  blur, home, Settings); the route differs from older tours because the user rearranged the home screen (fixed-coordinate taps
  land on other icons), still comparable inside 0042/0043 since every boot takes the same route. hyb8-p1-boot-1: 88.5 Hz,
  85 % single, gpu_ms 4.31, late +568, max 73.0 C, no DeviceLost. **But FUXI OVERLAP: commits=22800 skipped_waits=785 (3.4 %)
  kept_waits=22015**, leases=25295, lock_failed=0, refused=0 -> hyb8's overlap almost never applies, so hyb8 vs hyb7 does not
  test overlap yet.
- **Why the overlap is kept (dyld cache, QuartzCore):** only three functions call `addScheduledHandler:`
  (`hybrid-driver\tools\dsc\objcdis.py --sel addScheduledHandler:`): `MetalContext::delete_image` (whenever a command buffer
  is current, [ctx+0xb38] != 0, it defers the deleted image's releases: block_invoke_2 @0x1882e8de4 = dispatch_async([ctx+0xe98],
  block_invoke @0x1882ecc84 which objc_releases 3 objects) -> safe from any thread, at any later time),
  `copy_submitted_callbacks` (only on flush's no-wait commit path) and `flush` (behind the debug byte 0x1e1df779c). The 0020
  gate `!handlers && !fuxiOtherWork` therefore keeps the wait on every frame that deletes an image mid-encode.
  `objcdis.py` (new): disassembles a dyld-cache function by name fragment and names bl targets incl. __objc_stubs selectors.
- **Frontend patch 0023** (nmb/native d974ac0 on fc1d70e; harness `patches/nativemetal-fuxi-0023-overlap-handlers.patch`,
  workflow cp line after 0022, harness 3f79246 on codex/hybrid-lane, CI 43706249664): boot flag `fuxitudebug=...,ovsh` makes
  the gate `(!handlers || ovsh) && !fuxiOtherWork` (blit/compute still waits: those may write surfaces the leases do not
  name). The handlers still run, on the runtime's scheduled-handler thread after submission. FUXI OVERLAP gains (at the end,
  old fields unchanged) `waits_handlers waits_other waits_direct skips_handlers ovsh=0|1`: the waits that do happen in
  commitAndWaitUntilSubmitted by reason (fuxiOverlapReason ivar: 1 handlers, 2 blit/compute, 0 not queued).
- **0043 prepared** (`hybrid-driver\chain-0043.sh 43706249664`, running in the background, log chain-0043.log): waits for
  post-0042's POST_DONE (which pulls 20261115q, the 0043 base) and the CI, then build-0043.sh (base native-phone-20261115q,
  frontend 0023; checks `waits_handlers`/`ovsh` strings, SHA256SUMS vs native-layout0042-artifact: only the frontend may
  differ besides bridge/tarballs/log), deploy-layout0043.sh (generated from the 0042 scripts), then `boots-0043.txt`:
  tour-hyb8s, then p1 hyb8s / hyb8 / hyb8s / hyb8 (kernel-page1). Loaders: `native-hyb8s-p1-loader` (page1-spin + hyb8's
  swaps with fuxitudebug=nocb,ovsh; 605 bytes, payload df6b0e4f..., boot 578deeff...) and `native-tour-hyb8s-loader`
  (draws-spin + tour-hyb8's swaps with nocb,ovsh; 604 bytes, payload cb4ae746..., boot caac625e...); both pass
  verify-args-on-loader-payload.py.
- **0041 verdict (19:30).** p1 (script-metric.py): hyb8 boot-1 88.5 Hz (85 % single, gpu_ms 4.31, late +568, 73.0 C);
  hyb8 boot-2 invalid: the USB console stopped ~40 s into userspace (2693 lines, no FUXI TAPS loop, FUXI OVERLAP commits=1),
  Android only came back after reset-soon (truncated p1 logs happened before with hyb3/hyb4 too, so not counted against
  hyb8, but watch for repeats under hyb8/hyb8s). hyb7 boot-1 84.5 Hz (85 %, 4.35, +560, 71.0 C), boot-2 85.8 Hz (84 %, 4.36,
  +609, 69.4 C). No DeviceLost in any boot. `late +5xx` is a hyb7/hyb8 trait (0039 hyb7 had +563), not overlap.
  Stop-loss framing: hyb8 (one valid boot) is below 90 Hz, but with only 3.4 % of commits overlapped it has not tested
  overlap; the call moves to 0043's hyb8s (ovsh), whose overlap share must first be high. 0042's hyb8 p1 boots add samples.
- **hyb8i added to 0043 (19:40): GPU fence IRQ sleep.** Side analysis of the 0041 hyb8 boot-1 log (sweetprof PROF states +
  FUXI ABI lines): backboardd's threads spend a large share runnable-but-not-running (ready/live: queue-wait 27 %,
  completion 69.5 %, handlers 52.7 %, display 21 %), and queue-wait sits in the kernel WaitFence, which polls every
  `waitPollUs=100` us (FuxiAdrenoUserClient.cpp spin(): fence waits avg_us=3739, polls_avg=25, irq state 0). The kernels
  already contain `fuxi_gpu_irq.inc` (INTID 332, RBBM_INT_0 bit 20; storm protection, falls back to polling), enabled only
  by `fuxigpuncimport` bit 26 (67108864), never turned on. Loader `native-hyb8i-p1-loader` = hyb8 p1 with
  `fuxigpuncimport=304704=>fuxigpuncimport=69514824` (2405960 + bit 26; 601 bytes, payload 4370b952..., boot e648dfec...,
  passes verify-args-on-loader-payload.py). boots-0043.txt now: tour-hyb8s, then p1 hyb8s / hyb8 / hyb8i twice each,
  interleaved. Check: `FUXI GPU IRQ` attach line (armed/failed), `FUXI ABI: fence pending ... irq state` (1 = armed),
  wakes / timeouts / races, fence waits avg_us / polls_avg, prof-states.py ready share of queue-wait/completion, Hz, temp.
  Expectation kept modest: the 11-13 note (line ~1344) already judged the IRQ's latency gain at <= ~0.1 ms per wait; the
  reason to try it now is CPU pressure, not latency: 0041 p1 had ~30 polls per wait (fence-stats: avg_us ~4340,
  polls_avg ~29.7 in both hyb7 and hyb8), i.e. roughly 2-3k extra wakeups/s on cores where backboardd threads already
  queue (ready share above). New tool `hybrid-driver\tools\fence-stats.py <boot dirs>`: FUXI GPU IRQ attach lines, fence
  waits avg/max/polls (last and mean over all 600-wait lines), last irq state/why/n/wakes/timeouts/races/written/maskfix.
- **USB monitor drops (20:07) and reconnect fix.** 0042 tour-hyb8-boot-1 (with -TourShots) lost the host's probe at shot 1;
  tour-hyb8n-boot-1 (no shots) lost it ~25 s into the kernel (166 lines, still kernel_stage 4). Both: `usb.core.USBError:
  [Errno 2] Entity not found` in fuxi-xnu-monitor.py read_ram -> the monitor died, the reset deadline sent the phone back to
  Android. 3 of the last 4 boots dropped this way (0041 hyb8-p1-boot-2 too). Right after, Get-PnpDevice showed
  `Fuxi hardware probe` present and OK again -> a short re-enumeration, the phone kept running.
  Fix (ios-fuxi/tools/fuxi-xnu-monitor.py, uncommitted, backup in the session scratchpad): the main loop body is wrapped in
  `try/except usb.core.USBError`; on error it disposes the handle, polls `find_probe()` once a second for up to 90 s, checks the
  candidate answers 0x46 with `FXIMON01`, rebinds `dev` (the request()/read_ram closures read it) and continues from
  `last_total`; prints `[MONITOR] USB error ... (drop N)` / `[MONITOR] USB reconnect after X s` in the -monitor.log. Takes
  effect from 0042 tour-hyb8-boot-2 on. Note: a tour shot in flight when the drop happens is not retried.
- **Where 0041 hyb8 p1 loses frames (read while 0042 boots).** frame-summary: commit gaps 1 period 92.3 %, encode <2 ms
  91.5 % (light frames 784 us, heavy 2 % at 8.7 ms with 3.9 ms of resource creation) -> the CPU side commits at ~120 Hz.
  Flips: 85 % single / 9.7 % double (script-metric), SCANOUT waits gaps 489/57/3/51 per 600. So ~7 % of frames commit on
  time and flip a period late: the scanout copy (blit 616 us) waits `submit_to_start` avg 4466 us (max 13959) behind the
  frame's render (IBPROF span mean 4372 us, p50 4182, p90 6169, full GMEM via fuxitudebug=nocb). By target: 576x2560 IBs
  2205 us/job (54 %, ~165 draws and 9 passes each, ~1.9 per job), 1125x2436 676 us (3 draws, 1 pass), 1152x1920 515 us and
  1152x2560 427 us (~750 draws, sampled in ~20 % of jobs). Next GPU-side lever would be the 576x2560 passes' draws; the
  frame-pipelining levers (overlap 0023, IRQ fence) are what 0043 tests.
- **0042 prewarm A/B (tours, same image, 20:29 / 20:51).** tour-hyb8-boot-1 (-TourShots) and tour-hyb8n-boot-1 (no
  shots, dropped in the kernel) lost USB (see above); tour-hyb8-boot-2 (prewarm on) and tour-hyb8n-boot-2 (nopw) are
  complete (14 shots + done, no DeviceLost, no USB drop, reconnect not exercised). New tool
  `hybrid-driver\tools\tour-late.py <tour boots>`: per tour segment (ending at each shot) backboardd frames and late
  frames from the cumulative `FUXI MTL: backboardd frames= late=` lines (boundary error within one MTL line).
  First opens with comparable frame counts, hyb8 vs hyb8n late/frames: Today 0/579 vs 23/612, Spotlight 22/498 vs
  143/487, Control Center 7/307 vs 58/417, Clock 0/288 vs 55/317, home+App Library 3/330 vs 3/335, Calendar 0/176 both
  -> those four screens 29 vs 279 late frames: **prewarm cuts first-open late frames ~90 %.** Not comparable: Settings /
  General / About rendered 38/1/0 frames with prewarm vs 350/352/300 without (the Settings tap probably did not open the
  app in hyb8-boot-2; 0041 tour-hyb8 also had 0/1/112), so whole-tour totals (69/2843 vs 316/3964) are not a fair
  comparison. frame-summary: heavy frames making pipelines 17 (hyb8) vs 35 (hyb8n), 17+ ms frames 0.3 % both.
  tour-hyb8-boot-3 (prewarm on) adds a sample.
  **tour-hyb8-boot-3 (20:57-21:14, prewarm on)**: USB dropped once and the new monitor reconnected after 1.0 s (console
  from 421347), 14 shots + done, no DeviceLost. First opens Today/Spotlight/Control Center/Clock late 9/13/10/0 = 32
  (boot-2: 29, hyb8n-boot-2: 279) -> the ~90 % cut repeats.
- **Tour route never opens an app on this root (found 21:15).** fk_tour (kdraws-src xnu/fuxi-h1/fuxi_keys.c) taps the stock
  iOS 16 layout: Settings 670,1710, Clock 670,1140, Calendar 410,860, Photos 670,860. The user's home page 1 (0041 shot2):
  Photos 148,285, App Store 410,285, Settings 148,570, a Photos widget on the right, dock Phone/Safari/Messages at
  x 280/540/800, y ~2245; one home page (App Library next). So shots 9-14 (settings..photos) of every tour since 0022 show
  the home screen, and the hyb8n Settings/General/About frames (350/352/300) are not app frames either (cause unknown).
  The p1 preset @3 (tap 148,570) does open Settings (exec Preferences) and is unaffected.
- **Tour kernel fix (in CI, not booted).** kdraws-src branch `tour-layout` 9f48c57 (on 6fc377f = page1): fk_tap(148,570) for
  Settings, Back twice, then a status-bar tap (200,40) scrolls the list to the top; General/About keep fuxitourg (2100) /
  fuxitoura (400) - unverified on this layout (no Settings screenshot exists yet); then Photos 148,285, App Store
  410,285, Safari 540,2245, Messages 800,2245, Phone 280,2245 (16 shots; line count unchanged, 0103 copy edited the same
  way). Harness branch `codex/kernel-tour-layout` 3f9c327 (worktree xiaomi13-ios-gpu-20261108/harness-tour):
  patches/kernel-tour-layout.patch + workflow kernel-tour-layout.yml (same as kernel-bo-slots-check minus
  iomfb-analytics, artifact `fuxi-kernel-tour-layout`), CI 43718699612. Then: kernel-variant spin loader from
  native-page1-spin-loader, tour loaders by prepare-args-on-loader, -Seconds ~720 (16 shots at hold 30). First boot
  calibrates the General row from the settings shot. Kernel = page1 + tour, so p1 boots could share it.
- **0042 hyb8-p1-boot-1 (21:19-21:29): raw 82.6 Hz is a script-route artefact.** script-metric: 82.6 Hz (0041 hyb8 88.5),
  85 % single, gpu_ms 4.38, late +574, 70.2 C, no DeviceLost, overlap skipped 3.3 %, fence avg_us ~4392 polls 30 (irq off),
  frame-summary identical to 0041 (encode <2 ms 92 %, gap-1 92.5 %, heavy 409 at 8.9 ms). But loops 2, 11, 14 (besides
  loop 1, which every p1 boot has) did not have Settings in front: the scroll swipes pulled Spotlight (SWEET AKS:
  Spotlight / searchd), the known heavy case, and those windows ran 51-53 Hz. New tool
  `hybrid-driver/tools/p1-clean.py <boots>`: same Hz formula as script-metric, split into windows with/without a Spotlight
  or searchd AKS line. Clean-window Hz: 0042 hyb8-1 88.0; 0041 hyb8-1 89.3, hyb7 88.9 / 88.4; 0040 hyb4 85.2 / 86.7,
  hyb4t 87.0 / 87.5. Use the clean figure for 0042/0043 comparisons (and for the stop-loss call).
- **Tour kernel built (CI 43718699612 success, 13:35Z).** `kernel-tourl-artifact` (SHA256SUMS OK, kernel 2322a500...,
  cmdline.txt = page1's; fuxi.adt differs from page1's in 64 bytes like every build - the loaders take the ADT from the
  source payload anyway). Loaders: `native-tourl-spin-loader` = prepare-kernel-variant(native-page1-spin-loader,
  kernel-tourl-artifact) (args 542, payload 4074b4ae..., boot 6cd436a8...); `native-tourl-hyb8s-loader` =
  prepare-args-on-loader(native-tourl-spin-loader, tour-hyb8s's swaps.txt) -> args 604 identical to
  native-tour-hyb8s-loader, payload c28c9806..., boot 2b4272f1...; verify-args-on-loader-payload.py OK. Appended as the
  last line of boots-0043.txt: tourl-hyb8s-boot-1 (-Seconds 720 -Reset 15 -TourShots). Check: shot 9 settings shows the
  Settings top list (then read the General row y for fuxitourg), shots 12-16 the five apps, `exec Preferences`/
  `exec MobileSafari` etc. in boot.log.
- **0042 hyb8k-p1-boot-1 (21:44-21:57, kernel-iomfb-artifact): analytics fix works, Hz neutral.** script-metric 87.0 Hz
  raw, clean 89.4 Hz (Spotlight loops 1, 4), gpu_ms 4.31, 85 % single, 74.9 C, no DeviceLost, overlap 3.4 %. `SWEET FB:
  backboardd sel 68 ... n68` reaches 512 (1, 2, 4, ... 512; ~one per 62 swaps as designed). swap-ipc.py (display.primary
  live samples): hyb8 kern_SwapEnd 34.9 % / SwapEnd>SearchCFProperty 20.6 % / core_analytics 20.6 % -> hyb8k 16.2 % / 0 %
  / 0 %. Remaining IORegistryEntrySearchCFProperty 12.7 % is a different caller (scratchpad searchprop-callers.py +
  tools/dsc/regkeys.py): `CA::WindowServer::IOMFBDisplay::begin_update+0x620` -> IOMobileFramebufferCopyProperty(fb,
  "Brightness_Scale") -> kern_CopyProperty = IORegistryEntrySearchCFProperty(service, plane, key, NULL, options 0), one
  IPC per frame when flag bit 41 of the display's [x26] word is set; result read as int 16.16 (x 2^-16) into [x19+0x698],
  default 1.0 when absent. Publishing the property would not remove the IPC (options 0, no recursion), so no cheap
  kernel fix; it only shows again that one IOKit IPC costs tens of us on this port. Display-thread CPU is not what limits
  Hz (commit gaps 92 % single), so parked.
- **0042 p1 boot-2 pair (21:57-22:24): verdict - prewarm and the analytics fix are both Hz-neutral; hyb8 sits at ~89.**
  hyb8-p1-boot-2: raw 86.5, clean 89.1 Hz (Spotlight loops 1, 13), gpu_ms 4.34, 73.8 C, overlap 3.3 %, no DeviceLost.
  hyb8k-p1-boot-2: raw 87.7, clean 88.5 Hz (Spotlight loop 1), gpu_ms 4.32, 75.3 C, overlap 3.5 %, no DeviceLost;
  swap-ipc: kern_SwapEnd 21.8 %, core_analytics 0, SearchCFProperty 18.3 % (Brightness_Scale caller only). One USB drop
  (reconnect after 1.0 s, nothing lost). Clean-window means: hyb8 88.0 / 89.1, hyb8k 89.4 / 88.5, hyb8n 89.3 (one run) -
  all inside run-to-run noise (+-0.7). Overlap stays ~3.4 % in every 0042 run, so the stop-loss call waits for 0043's
  hyb8s (skip-on-handlers, must raise skipped %) and hyb8i (IRQ fence waits).
- **post-0042 (22:25-22:35).** Pull OK -> native-phone-20261115q-root.img, sha 4cced778..., backup
  fuxi_Backup/ios-root-phone-20261115q (index row added), old delta 20261115p removed. INSPECT guest run OK (~1 min), but
  extract-b64.py failed: a guest kernel line ("[   44.769575] watchdog: BUG: soft lockup - CPU#1 stuck for 26s!") landed
  inside one base64 line. Fixed extract-b64.py (strips `[ secs.usec ] ...` to end of line before filtering) and
  re-extracted pipelog-0042/pipelines.log (23.5 MB, 1039 P lines). POST_DONE was appended by hand so chain-0043 went on
  (CI_OK / BUILD 22:35:52). pipelog-boots.py: 0042's boots (#56-#65) spend 99-418 ms in total creating 111-178 render
  pipelines per boot (max single 12-73 ms) - not a Hz factor.
- **0043 tour-hyb8s-boot-1 (22:47-23:01, kernel-draws + frontend 0023 ovsh): overlap works, but late frames up.**
  overlap-stats: commits 2400, skipped 2385 (99.4 %) vs ~3.4 % for hyb8 - skips_handlers 2169, waits_handlers 0,
  waits_other 14, ovsh=1. No DeviceLost. Shots saved 1,2,3,4,6,9,11-14 (5,7,8,10 missing; 5 USB drops, each reconnect
  1.0 s). shot-diff vs 0041 tour-hyb8-boot-1: static shots identical (mean 0.02-0.03); today/clock/photos differ only
  because the old tour taps land on different screens (checked by eye: no corruption, Today widget/blur correct).
  tour-late vs 0042 tour-hyb8 boot-2/3: total late 274/3048 vs 69/2843, 64/2772 - today 67 (0/9), spotlight 49 (22/13),
  control-center 103 (7/10), clock 26 (0/0). Overlap may trade latency for throughput (or "late" now includes queue
  time); the p1 hyb8s boots decide.
  **Correction - "late" is not comparable across overlap rates.** BackboardGate.completed counts late when wallMS >
  16.67, and wall = frameNanos(_commitStart) at completion (Frontend.mm:1506). Without overlap CA waits for each
  frame, so wall is that frame's own encode+GPU time; with overlap the commit returns at once and the buffer can sit
  behind the previous frames in the worker queue, so wall also counts queue time. hyb8s overlaps 99 % of commits, hyb8
  3 %, so the extra late frames are mostly queue time (pipelining: at most ~1 frame more latency). Commit gaps are
  unchanged (gap-1 83.2 % vs 82.9 / 82.7 %; 2-gaps 3.8 % vs 1.8 %, 4+ 8.6 % vs 10.9 / 11.0 %). Judge hyb8s by p1 Hz /
  flip gaps, not late.
- **0043 hyb8s-p1-boot-1 (23:07-23:16): overlap 96.8 % but Hz unchanged - overlap is not the limiter.** raw 87.5,
  clean 88.2 Hz (Spotlight loop 1), gpu_ms 4.28, flip gaps 85 % single (same as hyb8), commit gaps 92.0 % single,
  74.9 C, no DeviceLost, no USB drop. overlap-stats: skipped 22080/22800 (96.8 %), skips_handlers 21293, waits_other
  719. vs hyb8 clean 88.0 / 89.1 / 89.3 / 89.4 / 88.5. So letting CA run ahead of the GPU on ~97 % of commits buys
  nothing: the missed flips (15 %) are not CA waiting on GPU completion. Pending: hyb8s-2 and hyb8i x2 to confirm.
- **Where the p1 time goes (tools/gap-time.py, gap-corr.py): the 4+ commit gaps are not render stalls.** FUXI FRAME
  gap buckets over the whole @3 script (1 / 2 / 3 / 4+ (~29-50 ms) / idle (>=50 ms)):
  0036 hyb3 77.5 Hz [17508, 2284, 95, 918, 194]; 0040 hyb4 84.9 [20897, 1298, 77, 958, 169];
  0041 hyb7 84.5 [21636, 599, 107, 873, 184]; 0042 hyb8 86.5 [22157, 665, 95, 912, 170];
  0043 hyb8s 87.5 [22073, 721, 88, 955, 162]. Every boot since 0036 has 4+ = 835-958 (median ~930), while encode
  >= 10 ms frames fell 2518 -> 913 -> 68-104 and 2-period gaps 2284 -> ~700. Per 600-frame block, 4+ correlates with
  idle (+0.38..+0.70, i.e. with step ends) but not consistently with heavy frames (+0.05..+0.79). So the 4+ gaps come
  from upstream of backboardd's render path (animation tails where motion is sub-pixel - cf. @2 capped at 90 px/s -
  or app-side CPU), not from encode/GPU. Time split for hyb8s: 1-period 66 %, 2-3 period ~5 %, 4+ 10-17 %, idle
  11-18 % (162 events ~ one per step). If 4+ is demand, the script ceiling is ~90.5 Hz (all 2-3 gaps fixed) and
  hyb8s is at ~97 % of it; a faster GPU path (P3) can only claim the 2-3 period part. flog window (128 frames after
  3000 swaps, Settings scroll) has no 4+ gap; it shows the same 8x 2-period run in hyb8 and hyb8s (deterministic).
  Not settled: tail vs app-side CPU - needs a per-step gap log (frontend gap sequence with script-step marks).
- **0043 hyb8-p1-boot-1 (23:17-23:27, A/B partner of hyb8s): clean 88.1 Hz = hyb8s 88.2.** raw 85.9 (Spotlight in
  loop 17, 2 windows 49.5 Hz), gpu_ms 4.33, flip gaps 85 % single, commit gaps 92.6 % single, overlap 3.4 %
  (waits_handlers 21334), FRAME buckets [22221, 590, 84, 936, 168] (4+ again ~930), 70.6 C, no DeviceLost. 1 USB drop
  after the script had finished (reconnect 1.0 s, data complete). tools/analyze-p1.sh runs the whole set per boot.
- **0043 hyb8i-p1-boot-1 (23:31-23:45, IRQ fence sleep bit 26): clean 88.4 Hz - same as hyb8 88.1 / hyb8s 88.2.**
  raw 86.3 (Spotlight loops 1 and 19), gpu_ms 4.33, flip gaps 85 % single, overlap 3.3 %, FRAME buckets
  [22175, 664, 85, 900, 175], 71.0 C (hyb8 70.6), boosted 169.6 s, no DeviceLost, no USB drop. IRQ path works:
  INTID 332 armed, state=1, n=24017 irqs, wakes 23006, timeouts 75829 (~3.3 short sleeps per wait), races 0;
  fence waits avg 4316 us, polls_avg 5. heavy frames encode 8.8 ms / make 3.6 ms (hyb8 10.7 / 5.7 - maybe noise).
  So interrupt-driven fence waits neither raise Hz nor lower temperature on this script.
- **0043 hyb8s-p1-boot-2 (23:46-23:59): clean 88.5 Hz** (boot-1 88.2). raw 87.7 (Spotlight loop 1), overlap 96.9 %,
  flip gaps 86 % single, FRAME [22205, 591, 84, 953, 166], 73.8 C, no DeviceLost, 1 USB drop after the script.
- **0043 hyb8-p1-boot-2 (23:59-00:13): clean 88.5 Hz** (boot-1 88.1). raw 87.8 (Spotlight loop 1), overlap 3.4 %,
  flip gaps 85 % single, FRAME [22208, 612, 81, 931, 167], 74.2 C, no DeviceLost, no USB drop.
- **On-screen rate is ~3.5 % below the MTL Hz in every version (tools/shown-rate.py).** SweetFramebuffer "stats 600
  frames ... S swaps, U not shown": unshown frames are all `replaced` (ready, waiting for START, superseded by a newer
  frame): 0025 vt6tc 4.2 %, 0034 hyb4 4.0 %, 0036 hyb3 3.7 %, 0040 hyb4 4.8 %, 0041 hyb7 3.8 %, hyb8 3.2-3.9 %, hyb8s
  3.6-3.9 %, hyb8i 3.5 %. So comparisons across versions hold; on-screen Hz ~ MTL x 0.964 (hyb8 ~84.6 raw).
  Flip 2-period gaps fell 4894 (hyb3) -> 3343 (hyb4) -> ~2190 (hyb8), but in hyb8 only ~610 of them are commit 2-gaps:
  ~1600 frames per run are committed on time yet finish (worker encode + GPU + copy) after the TE budget - one frame
  shown twice (or replaced). stages: swap->ready ~0, copy submit->fence ~5.2 ms (copy queued behind the frame's GPU
  job), fence->START ~3.7 ms. These late frames are the mid/heavy ones (encode >= ~5 ms on the worker).
- **0043 hyb8i-p1-boot-2 (00:13-00:26): clean 88.1 Hz** (boot-1 88.4). raw 85.8 (Spotlight loop 1, 24.9 Hz window),
  flip gaps 86 % single, FRAME [22205, 620, 73, 942, 159], unshown 3.3 %, IRQ n=25027 wakes 24100 timeouts 79671
  races 0, 71.0 C governor (perf state high late, 6 boosts), no DeviceLost, no USB drop.
- **0043 STOP-LOSS VERDICT (00:30): all variants <= 90 Hz -> user decides P3 vs stop.** clean Hz: hyb8 88.1 / 88.5,
  hyb8s 88.2 / 88.5, hyb8i 88.4 / 88.1 (all within +-0.4). Overlap (97 %) and IRQ fence waits are both neutral.
  Report: hybrid-driver/STOPLOSS-0043.zh-TW.md. My recommendation: no P3 (it saves per-draw recording; the script
  ceiling is ~90.5 Hz because 4+ gaps (~930/run, invariant hyb3..hyb8s) and idle are upstream of rendering; the
  visible hitches are ~1600 late mid/heavy frames per run whose biggest cost is texture creation + IOSurface import,
  not draw recording). Cheaper next items if GPU work continues: per-step gap log to classify the 4+ gaps; heavy-frame
  texture/import reuse.
- **CPU clocks are an untested lever since the GPU got fast (user asked whether app open/close CPU overrun is "elsewhere").**
  0043 hyb8-p1-boot-2 FUXI PERF (fuxiperf=2: hot 660 / limit 720, budget 1000, base = legacy cool 1401.6 MHz):
  unboosted gold 1401 / prime 1708 MHz; boosted windows avg gold 1415-1721 (max 2323), prime 1170-1471 (max 2592);
  boosted 150-170 s of the 275 s script; "state high" with cuts late in the boot. Measured maxima without governor
  (11-13 thermal.jsonl): gold 2707, prime 2957 MHz. The 11-14 "more boost changed nothing" A/B was GPU-bound
  (gpu_ms 7.5-8.5); now gpu_ms 4.3 and the late frames are CPU-heavy, so it needs a re-test. Kernel ABI costs are small:
  whole boot import 238 calls 114 ms (avg 479 us, max 1.5 ms), free 175 ms, alloc 118 ms, map 53 ms (~20 % of heavy-frame
  make time); the rest of make is user-space texture creation. Hypothesis: low clocks also slow the launching app, which
  would explain why the 4+ gaps do not move with driver changes. Proposed A/B (needs args room, p1 args ~600/608):
  hyb8 + fuxiperfbase=2400 (+ fuxiperfmax 760), compare Hz / flip single / 4+ count / unshown / temperature.
- **0043 tourl-hyb8s (app-launch tour, 720 s, -TourShots), 2026-11-16 00:27-00:46.** 9 USB drops, all reconnected in
  1.0 s, but each drop killed the 16 s framebuffer transfer in flight: only shots 3, 4, 7, 12, 13, 15, 16 were saved
  (lost: 1, 2, 5, 6, 8, 9 settings, 10, 11, 14 safari), so the Settings top-list / General-row y check could not be done.
  boot.log shows every app exec'd (Preferences, MobileSlideShow, AppStore, MobileSafari, MobileSMS, MobilePhone).
  Saved shots: Messages renders correctly ("沒有訊息" list); App Store shows its "載入中" spinner (airplane mode, expected);
  Photos and Phone stay all black for 16+ s (the -b shot shows only the dock animating). Photos: assetsd aborts
  (sig 6, same pc/bt every time, 3x this boot, 2x in tour-hyb8s) and the photos segment produced 1 frame -> Photos is
  blocked by assetsd, not by the GPU path. Phone: no crash line, 325 frames, black UI (probably telephony/CommCenter
  side). Background crash loop fairplayd.H2 / bluetoothd / vmd sig 9 (~65x each per boot) is the same in both tours.
  tour-late: total late 171 / 5599 frames (tour-hyb8s 274 / 3048); control-center 71 / 682, messages 28 / 411.
  Compared with tour-hyb8s, where the Settings tap never opened the app (0 frames), the tourl exec path does launch
  Preferences (settings 140 fr, about 491 fr). Usability items for later: assetsd abort (Photos), MobilePhone black UI,
  tour shot capture vs USB drops (save shots to phone storage or retry after reconnect).
- **User report after tourl-hyb8s (00:51): "剛剛進入鎖定屏幕自訂時又掉了一次gpu"; before that "手動滑動反覆打開軟體測試".**
  NOT in the log: the host console capture ended at +712 s (boot.log mtime 00:43:41, monitor_exit=0); the phone stayed
  in iOS until the chain's reset (reset deadline +1 min, ALL_DONE 00:52:05). boot.log after `FUXI TOUR: done` (7561-7866)
  has no SWEET HID touch-down, no exec of apps, no DeviceLost / runtime-failure, backboardd idle (hz 0) -> all of the
  user's manual use (~00:44-00:51) was unrecorded. History: 11-14 two DeviceLost in 6 manual boots, both with the user in
  the wallpaper / lock-screen customization (AegirPoster / PosterBoard sig 6 crash pairs before the loss), 0039 hyb7
  one DeviceLost in a scripted p1 (no poster); 0041+ prints the refusal reason (`: {refusal}`) so the next loss is named
  IF it lands inside a recorded window. To catch it: a user boot (no script) with recording for the whole time the
  user holds the phone (-Seconds 1800), user repeats "open/close apps -> lock-screen customization".

## 2026-11-16 06:20-06:35 (000 line) slot-b chain-v2 = daily boot with battery + brightness (boot_b WRITTEN)

- User approved merging the verified 000-line features into the daily (standalone, slot-b) iOS boot. The old note
  "slot-b chain still carries native-auto-cool-loader's args" is now stale.
- Payload loader `native-dly1-user-loader` (build: `work\adsp-battery\build-daily1-loader.sh`) = native-vt6tc-user-loader
  args with `debug=0xc=>debug=12` + `fuxibacklight=4` (607 chars), kernel `kernel-adsp-bl5-artifact` (ksrc adsp-battery
  7928af8, sha 3e0a5299...; tour code inert without fuxitour), ADT `fuxi,adsp=3` only (no fuxi,amp). boot.img ce7554ff...
- `work\boot-chain\build-chain.py` gained `--payload <loader dir>` (default still native-auto-cool-loader = chain-v1);
  `chain-v2` = `--payload native-dly1-user-loader`, boot.img `994c95d6b78118ae625bb5919da2c7e416a5644fb403abc655ec4c6a85bcf25d`
  (66326528 bytes, same size as v1; Android Image and layout identical: linux at 0x1200000, image end 0x3fe0000).
  `ksu-ios-entry\build-module.py` now reads the payload loader from the chain's build.txt.
- Test: `fastboot boot` of chain-v2 (dir `native-chainv2-user-loader`, 06:23-06:32, `native-chainv2-user-loader-boot-1`):
  normal path -> auto-launch -> SpringBoard; SweetBattery static fallback -> ADSP battery manager, SpringBoard matched
  IOPMPowerSource -> SweetBattery; backboardd set brightness 1459 (iOS-saved value) -> DCS sent in 70 us; touch at 10 s;
  0 panic, 0 USB drop, 0 DeviceLost.
- Deploy (`work\adsp-battery\hw000\hw000-deploy-bootb.sh`, log next to it): before boot_b = chain-v1 9924fe1c...,
  boot_a stock 3d957296...; dd of the first 66326528 bytes; readback boot_b = 994c95d6..., boot_a unchanged. KSU module
  fuxi_ios_boot -> v3 (loader.sha256 / loader.size / expected-fiemap.txt replaced in place under /data/adb/modules;
  host zip `fuxi_ios_boot-chain-v2.zip`); steps 1-3 of action.sh dry-run on the phone: OK; active slot still a.
  iOS root NOT touched (user data kept). Restore point: `fuxi_Backup\boot_b-chain-v1-20261116` (v1 image + v1 zip).
- Not verified: an actual slot-b normal boot of chain-v2 (no USB there) - next time the user presses the KSU action.
- For 001: if you rebuild the slot-b chain for a new daily config, start from `--payload native-dly1-user-loader`
  (or re-apply `+fuxibacklight=4` and ADT `fuxi,adsp=3` on your new payload) so battery and brightness stay on.

## 2026-11-16 07:00-08:50 (000 line) user report "brightness stops after two drags, unplugged still 100% charging" -> chain-v3

- User's daily (slot-b chain-v2) report: brightness responded twice then stopped; unplugged still showed 100% charging;
  no battery history in Settings. The earlier "verified" claims for battery / USB charging / brightness were only
  USB-tethered fastboot boots (always plugged, one or two slider moves). Corrected in `work\hw000-adaptation-status.zh-TW.md`.
- Brightness: old kernel halted DCS for the whole boot on the first send without a clean completion (~1/250). ksrc
  ba65d38 clears errors like Linux dsi_error() and halts only after 6 in a row; ksrc 7af418a waits up to 20 ms for
  CMD_DMA_DONE (a command can queue behind one MDP frame: bl7t saw 4 completions at ~7.9 ms, irq 0x501). bl6t ~2900 sends,
  bl7t ~3600 requests incl. 8 tour drags: no halt, no "no clean completion" in bl7t.
- Battery: bl6t/bl7t (ADT fuxi,adsp=4 staged reading) prove iOS follows SweetBattery (CC shows 57%, no bolt). An actual
  unplug was never tested (all test boots tethered). Plugged: ADSP status=1 at 100% -> iOS "charging" is real top-off.
  Hypotheses for the standalone symptom: ADSP never delivered (static fallback is 100% external=1), or BmUsbOnline went
  stale (ShengBattmgr charge_known keeps the last value when a USB_GET read errors / times out). Needs the mtdblk log.
- Standalone log: Fuxi has no logdump. fx_log (ksrc d96adda, sweet_ufs.c, patch 0106; ADT chosen fuxi,log=1) writes
  the console every 10 s to the first 2049 blocks of `mtdblk` (sda19, LUN 0, 32 MiB, all zeros before, backup
  `fuxi_Backup\mtdblk-20261116`), only if block 0 carries "FUXILOG1 STAMPED" at byte 2048 (stamped 07:46).
  `work\adsp-battery\hw000\fuxi-log.sh survey|stamp|pull` + `fuxi-log-parse.py`. bl7t pull = superset of the USB log.
  Each boot overwrites the previous one: pull before the next iOS boot.
- Kernel: harness codex/adsp-battery f6f2593, CI 43776623221 -> `kernel-adsp-bl7-artifact` (8579ba89...).
  Loaders: `native-bl7t-user-loader` (build-bl7t-loader.sh, test only), `native-dly2-user-loader` (build-daily2-loader.sh:
  vt6tc-user args, debug=12, +fuxibacklight=4, ADT fuxi,adsp=3 + fuxi,log=1, 607 chars).
- chain-v3 = `build-chain.py chain-v3 --payload native-dly2-user-loader`, boot.img 2965b2cc...c27c (66326528 bytes).
  fastboot-boot test (`native-chainv3-user-loader-boot-1`): SpringBoard, ADSP battery, brightness 1463 sent, 0 panic,
  0 USB drop. Written to boot_b 08:47 (`hw000-deploy-bootb-v3.sh`): readback OK, boot_a unchanged, KSU module v4,
  action.sh steps 1-3 dry-run OK, active slot a. Restore point `fuxi_Backup\boot_b-chain-v2-20261116` (image + module tar).
- For 001: a new daily payload must keep `+fuxibacklight=4` and ADT `fuxi,adsp=3` + `fuxi,log=1`.
- Open: unplug behaviour (user standalone boot + `fuxi-log.sh pull`), battery history (PerfPowerServices, 1970 clock).
