// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/unique_fd.h>
#include <capntrips/snapshot/snapshot.pb.h>
#include <fs_mgr_dm_linear.h>
#include <libdm/dm.h>
#include <libfiemap/image_manager.h>
#include <liblp/builder.h>
#include <liblp/liblp.h>
#include <update_engine/update_metadata.pb.h>

#include <libsnapshot/auto_device.h>
#include <libsnapshot/device_info.h>
#include <libsnapshot/return.h>
#include <libsnapshot/snapshot_writer.h>
#include <snapuserd/snapuserd_client.h>

#ifndef FRIEND_TEST
#define FRIEND_TEST(test_set_name, individual_test) \
    friend class test_set_name##_##individual_test##_Test
#define DEFINED_FRIEND_TEST
#endif

namespace android {

namespace fiemap {
class IImageManager;
}  // namespace fiemap

namespace fs_mgr {
struct CreateLogicalPartitionParams;
class IPartitionOpener;
}  // namespace fs_mgr

// Forward declare IBootControl types since we cannot include only the headers
// with Soong. Note: keep the enum width in sync.
namespace hardware {
namespace boot {
namespace V1_1 {
enum class MergeStatus : int32_t;
}  // namespace V1_1
}  // namespace boot
}  // namespace hardware

}  // namespace android

namespace capntrips {
namespace snapshot {

struct AutoDeleteCowImage;
struct AutoDeleteSnapshot;
struct AutoDeviceList;
struct PartitionCowCreator;
class ISnapshotMergeStats;
class SnapshotMergeStats;
class SnapshotStatus;

static constexpr const std::string_view kCowGroupName = "cow";
static constexpr char kVirtualAbCompressionProp[] = "ro.virtual_ab.compression.enabled";

bool OptimizeSourceCopyOperation(const chromeos_update_engine::InstallOperation& operation,
                                 chromeos_update_engine::InstallOperation* optimized);

enum class CreateResult : unsigned int {
    ERROR,
    CREATED,
    NOT_CREATED,
};

class SnapshotManager final {
    using CreateLogicalPartitionParams = android::fs_mgr::CreateLogicalPartitionParams;
    using IPartitionOpener = android::fs_mgr::IPartitionOpener;
    using LpMetadata = android::fs_mgr::LpMetadata;
    using Partition = android::fs_mgr::Partition;
    using MetadataBuilder = android::fs_mgr::MetadataBuilder;
    using DeltaArchiveManifest = chromeos_update_engine::DeltaArchiveManifest;
    using MergeStatus = android::hardware::boot::V1_1::MergeStatus;
    using FiemapStatus = android::fiemap::FiemapStatus;

    friend class SnapshotMergeStats;

  public:
    ~SnapshotManager();

    // Return a new SnapshotManager instance, or null on error. The device
    // pointer is owned for the lifetime of SnapshotManager. If null, a default
    // instance will be created.
    static std::unique_ptr<SnapshotManager> New(DeviceInfo* device = nullptr);

    // This is similar to New(), except designed specifically for first-stage
    // init or recovery.
    static std::unique_ptr<SnapshotManager> NewForFirstStageMount(DeviceInfo* device = nullptr);

    // Helper function for first-stage init to check whether a SnapshotManager
    // might be needed to perform first-stage mounts.
    static bool IsSnapshotManagerNeeded();

    // Helper function for second stage init to restorecon on the rollback indicator.
    static std::string GetGlobalRollbackIndicatorPath();

    // Detach dm-user devices from the current snapuserd, and populate
    // |snapuserd_argv| with the necessary arguments to restart snapuserd
    // and reattach them.
    bool DetachSnapuserdForSelinux(std::vector<std::string>* snapuserd_argv);

    // Perform the transition from the selinux stage of snapuserd into the
    // second-stage of snapuserd. This process involves re-creating the dm-user
    // table entries for each device, so that they connect to the new daemon.
    // Once all new tables have been activated, we ask the first-stage daemon
    // to cleanly exit.
    bool PerformSecondStageInitTransition();

    bool BeginUpdate();
    bool CancelUpdate();
    bool FinishedSnapshotWrites(bool wipe);
    void UpdateCowStats(ISnapshotMergeStats* stats);
    MergeFailureCode ReadMergeFailureCode();
    bool InitiateMerge();
    UpdateState ProcessUpdateState(const std::function<bool()>& callback = {},
                                   const std::function<bool()>& before_cancel = {});
    UpdateState GetUpdateState(double* progress = nullptr);
    bool UpdateUsesCompression();
    bool UpdateUsesUserSnapshots();
    Return CreateUpdateSnapshots(const std::string& target_partition_name, uint64_t snapshot_size);
    bool DeleteSnapshot(const std::string& name);
    bool MapUpdateSnapshot(const CreateLogicalPartitionParams& params,
                           std::string* snapshot_path);
    std::unique_ptr<ISnapshotWriter> OpenSnapshotWriter(
            const android::fs_mgr::CreateLogicalPartitionParams& params,
            const std::optional<std::string>& source_device);
    bool UnmapUpdateSnapshot(const std::string& target_partition_name);
    bool NeedSnapshotsInFirstStageMount();
    bool CreateLogicalAndSnapshotPartitions(
            const std::string& super_device,
            const std::chrono::milliseconds& timeout_ms = {});
    bool HandleImminentDataWipe(const std::function<void()>& callback = {});
    bool FinishMergeInRecovery();
    CreateResult RecoveryCreateSnapshotDevices();
    CreateResult RecoveryCreateSnapshotDevices(
            const std::unique_ptr<AutoDevice>& metadata_device);
    bool Dump(std::ostream& os);
    std::unique_ptr<AutoDevice> EnsureMetadataMounted();
    ISnapshotMergeStats* GetSnapshotMergeStatsInstance();
    bool MapAllSnapshots(const std::chrono::milliseconds& timeout_ms = {});
    bool UnmapAllSnapshots();
    std::string ReadSourceBuildFingerprint();

    // We can't use WaitForFile during first-stage init, because ueventd is not
    // running and therefore will not automatically create symlinks. Instead,
    // we let init provide us with the correct function to use to ensure
    // uevents have been processed and symlink/mknod calls completed.
    void SetUeventRegenCallback(std::function<bool(const std::string&)> callback) {
        uevent_regen_callback_ = callback;
    }

    // If true, compression is enabled for this update. This is used by
    // first-stage to decide whether to launch snapuserd.
    bool IsSnapuserdRequired();

    enum class SnapshotDriver {
        DM_SNAPSHOT,
        DM_USER,
    };

    // Add new public entries above this line.

    // Helpers for failure injection.
    using MergeConsistencyChecker =
            std::function<MergeFailureCode(const std::string& name, const SnapshotStatus& status)>;

    void set_merge_consistency_checker(MergeConsistencyChecker checker) {
        merge_consistency_checker_ = checker;
    }
    MergeConsistencyChecker merge_consistency_checker() const { return merge_consistency_checker_; }

  private:
    friend struct AutoDeleteCowImage;
    friend struct AutoDeleteSnapshot;
    friend struct PartitionCowCreator;

    using DmTargetSnapshot = android::dm::DmTargetSnapshot;
    using IImageManager = android::fiemap::IImageManager;
    using SnapuserdClient = android::snapshot::SnapuserdClient;
    using TargetInfo = android::dm::DeviceMapper::TargetInfo;

    explicit SnapshotManager(DeviceInfo* info);

    // This is created lazily since it can connect via binder.
    bool EnsureImageManager();

    // Ensure we're connected to snapuserd.
    bool EnsureSnapuserdConnected();

    // Helpers for first-stage init.
    const std::unique_ptr<DeviceInfo>& device() const { return device_; }

    // Helper functions for tests.
    IImageManager* image_manager() const { return images_.get(); }
    void set_use_first_stage_snapuserd(bool value) { use_first_stage_snapuserd_ = value; }

    // Since libsnapshot is included into multiple processes, we flock() our
    // files for simple synchronization. LockedFile is a helper to assist with
    // this. It also serves as a proof-of-lock for some functions.
    class LockedFile final {
      public:
        LockedFile(const std::string& path, android::base::unique_fd&& fd, int lock_mode)
            : path_(path), fd_(std::move(fd)), lock_mode_(lock_mode) {}
        ~LockedFile();
        int lock_mode() const { return lock_mode_; }

      private:
        std::string path_;
        android::base::unique_fd fd_;
        int lock_mode_;
    };
    static std::unique_ptr<LockedFile> OpenFile(const std::string& file, int lock_flags);

    SnapshotDriver GetSnapshotDriver(LockedFile* lock);

    // Create a new snapshot record. This creates the backing COW store and
    // persists information needed to map the device. The device can be mapped
    // with MapSnapshot().
    //
    // |status|.device_size should be the size of the base_device that will be passed
    // via MapDevice(). |status|.snapshot_size should be the number of bytes in the
    // base device, starting from 0, that will be snapshotted. |status|.cow_file_size
    // should be the amount of space that will be allocated to store snapshot
    // deltas.
    //
    // If |status|.snapshot_size < |status|.device_size, then the device will always
    // be mapped with two table entries: a dm-snapshot range covering
    // snapshot_size, and a dm-linear range covering the remainder.
    //
    // All sizes are specified in bytes, and the device, snapshot, COW partition and COW file sizes
    // must be a multiple of the sector size (512 bytes).
    bool CreateSnapshot(LockedFile* lock, PartitionCowCreator* cow_creator, SnapshotStatus* status);

    // |name| should be the base partition name (e.g. "system_a"). Create the
    // backing COW image using the size previously passed to CreateSnapshot().
    Return CreateCowImage(LockedFile* lock, const std::string& name);

    // Map a snapshot device that was previously created with CreateSnapshot.
    // If a merge was previously initiated, the device-mapper table will have a
    // snapshot-merge target instead of a snapshot target. If the timeout
    // parameter greater than zero, this function will wait the given amount
    // of time for |dev_path| to become available, and fail otherwise. If
    // timeout_ms is 0, then no wait will occur and |dev_path| may not yet
    // exist on return.
    bool MapSnapshot(LockedFile* lock, const std::string& name, const std::string& base_device,
                     const std::string& cow_device, const std::chrono::milliseconds& timeout_ms,
                     std::string* dev_path);

    // Create a dm-user device for a given snapshot.
    bool MapDmUserCow(LockedFile* lock, const std::string& name, const std::string& cow_file,
                      const std::string& base_device, const std::string& base_path_merge,
                      const std::chrono::milliseconds& timeout_ms, std::string* path);

    // Map the source device used for dm-user.
    bool MapSourceDevice(LockedFile* lock, const std::string& name,
                         const std::chrono::milliseconds& timeout_ms, std::string* path);

    // Map a COW image that was previous created with CreateCowImage.
    std::optional<std::string> MapCowImage(const std::string& name,
                                           const std::chrono::milliseconds& timeout_ms);

    // Remove the backing copy-on-write image and snapshot states for the named snapshot. The
    // caller is responsible for ensuring that the snapshot is unmapped.
    bool DeleteSnapshot(LockedFile* lock, const std::string& name);

    // Unmap a snapshot device previously mapped with MapSnapshotDevice().
    bool UnmapSnapshot(LockedFile* lock, const std::string& name);

    // Unmap a COW image device previously mapped with MapCowImage().
    bool UnmapCowImage(const std::string& name);

    // Unmap a COW and remove it from a MetadataBuilder.
    void UnmapAndDeleteCowPartition(MetadataBuilder* current_metadata);

    // Remove invalid snapshots if any
    void RemoveInvalidSnapshots(LockedFile* lock);

    // Unmap and remove all known snapshots.
    bool RemoveAllSnapshots(LockedFile* lock);

    // List the known snapshot names.
    bool ListSnapshots(LockedFile* lock, std::vector<std::string>* snapshots,
                       const std::string& suffix = "");

    // Check for a cancelled or rolled back merge, returning true if such a
    // condition was detected and handled.
    bool HandleCancelledUpdate(LockedFile* lock, const std::function<bool()>& before_cancel);

    // Helper for HandleCancelledUpdate. Assumes booting from new slot.
    bool AreAllSnapshotsCancelled(LockedFile* lock);

    // Determine whether partition names in |snapshots| have been flashed and
    // store result to |out|.
    // Return true if values are successfully retrieved and false on error
    // (e.g. super partition metadata cannot be read). When it returns true,
    // |out| stores true for partitions that have been flashed and false for
    // partitions that have not been flashed.
    bool GetSnapshotFlashingStatus(LockedFile* lock, const std::vector<std::string>& snapshots,
                                   std::map<std::string, bool>* out);

    // Remove artifacts created by the update process, such as snapshots, and
    // set the update state to None.
    bool RemoveAllUpdateState(LockedFile* lock, const std::function<bool()>& prolog = {});

    // Interact with /metadata/ota.
    std::unique_ptr<LockedFile> OpenLock(int lock_flags);
    std::unique_ptr<LockedFile> LockShared();
    std::unique_ptr<LockedFile> LockExclusive();
    std::string GetLockPath() const;

    // Interact with /metadata/ota/state.
    UpdateState ReadUpdateState(LockedFile* file);
    SnapshotUpdateStatus ReadSnapshotUpdateStatus(LockedFile* file);
    bool WriteUpdateState(LockedFile* file, UpdateState state,
                          MergeFailureCode failure_code = MergeFailureCode::Ok);
    bool WriteSnapshotUpdateStatus(LockedFile* file, const SnapshotUpdateStatus& status);
    std::string GetStateFilePath() const;

    // Interact with /metadata/ota/merge_state.
    // This file contains information related to the snapshot merge process.
    std::string GetMergeStateFilePath() const;

    // Helpers for merging.
    MergeFailureCode MergeSecondPhaseSnapshots(LockedFile* lock);
    MergeFailureCode SwitchSnapshotToMerge(LockedFile* lock, const std::string& name);
    MergeFailureCode RewriteSnapshotDeviceTable(const std::string& dm_name);
    bool MarkSnapshotMergeCompleted(LockedFile* snapshot_lock, const std::string& snapshot_name);
    void AcknowledgeMergeSuccess(LockedFile* lock);
    void AcknowledgeMergeFailure(MergeFailureCode failure_code);
    MergePhase DecideMergePhase(const SnapshotStatus& status);
    std::unique_ptr<LpMetadata> ReadCurrentMetadata();

    enum class MetadataPartitionState {
        // Partition does not exist.
        None,
        // Partition is flashed.
        Flashed,
        // Partition is created by OTA client.
        Updated,
    };
    // Helper function to check the state of a partition as described in metadata.
    MetadataPartitionState GetMetadataPartitionState(const LpMetadata& metadata,
                                                     const std::string& name);

    // Note that these require the name of the device containing the snapshot,
    // which may be the "inner" device. Use GetsnapshotDeviecName().
    bool QuerySnapshotStatus(const std::string& dm_name, std::string* target_type,
                             DmTargetSnapshot::Status* status);
    bool IsSnapshotDevice(const std::string& dm_name, TargetInfo* target = nullptr);

    // Internal callback for when merging is complete.
    bool OnSnapshotMergeComplete(LockedFile* lock, const std::string& name,
                                 const SnapshotStatus& status);
    bool CollapseSnapshotDevice(LockedFile* lock, const std::string& name,
                                const SnapshotStatus& status);

    struct MergeResult {
        explicit MergeResult(UpdateState state,
                             MergeFailureCode failure_code = MergeFailureCode::Ok)
            : state(state), failure_code(failure_code) {}
        UpdateState state;
        MergeFailureCode failure_code;
    };

    // Only the following UpdateStates are used here:
    //   UpdateState::Merging
    //   UpdateState::MergeCompleted
    //   UpdateState::MergeFailed
    //   UpdateState::MergeNeedsReboot
    MergeResult CheckMergeState(const std::function<bool()>& before_cancel);
    MergeResult CheckMergeState(LockedFile* lock, const std::function<bool()>& before_cancel);
    MergeResult CheckTargetMergeState(LockedFile* lock, const std::string& name,
                                      const SnapshotUpdateStatus& update_status);
    MergeFailureCode CheckMergeConsistency(LockedFile* lock, const std::string& name,
                                           const SnapshotStatus& update_status);

    // Get status or table information about a device-mapper node with a single target.
    enum class TableQuery {
        Table,
        Status,
    };
    bool GetSingleTarget(const std::string& dm_name, TableQuery query,
                         android::dm::DeviceMapper::TargetInfo* target);

    // Interact with status files under /metadata/ota/snapshots.
    bool WriteSnapshotStatus(LockedFile* lock, const SnapshotStatus& status);
    bool ReadSnapshotStatus(LockedFile* lock, const std::string& name, SnapshotStatus* status);
    std::string GetSnapshotStatusFilePath(const std::string& name);

    std::string GetSnapshotBootIndicatorPath();
    std::string GetRollbackIndicatorPath();
    std::string GetForwardMergeIndicatorPath();
    std::string GetOldPartitionMetadataPath();

    const LpMetadata* ReadOldPartitionMetadata(LockedFile* lock);

    bool MapAllPartitions(LockedFile* lock, const std::string& super_device, uint32_t slot,
                          const std::chrono::milliseconds& timeout_ms);

    // Reason for calling MapPartitionWithSnapshot.
    enum class SnapshotContext {
        // For writing or verification (during update_engine).
        Update,

        // For mounting a full readable device.
        Mount,
    };

    struct SnapshotPaths {
        // Target/base device (eg system_b), always present.
        std::string target_device;

        // COW name (eg system_cow). Not present if no COW is needed.
        std::string cow_device_name;

        // dm-snapshot instance. Not present in Update mode for VABC.
        std::string snapshot_device;
    };

    // Helpers for OpenSnapshotWriter.
    std::unique_ptr<ISnapshotWriter> OpenCompressedSnapshotWriter(
            LockedFile* lock, const std::optional<std::string>& source_device,
            const std::string& partition_name, const SnapshotStatus& status,
            const SnapshotPaths& paths);
    std::unique_ptr<ISnapshotWriter> OpenKernelSnapshotWriter(
            LockedFile* lock, const std::optional<std::string>& source_device,
            const std::string& partition_name, const SnapshotStatus& status,
            const SnapshotPaths& paths);

    // Map the base device, COW devices, and snapshot device.
    bool MapPartitionWithSnapshot(LockedFile* lock, CreateLogicalPartitionParams params,
                                  SnapshotContext context, SnapshotPaths* paths);

    // Map the COW devices, including the partition in super and the images.
    // |params|:
    //    - |partition_name| should be the name of the top-level partition (e.g. system_b),
    //            not system_b-cow-img
    //    - |device_name| and |partition| is ignored
    //    - |timeout_ms| and the rest is respected
    // Return the path in |cow_device_path| (e.g. /dev/block/dm-1) and major:minor in
    // |cow_device_string|
    bool MapCowDevices(LockedFile* lock, const CreateLogicalPartitionParams& params,
                       const SnapshotStatus& snapshot_status, AutoDeviceList* created_devices,
                       std::string* cow_name);

    // The reverse of MapCowDevices.
    bool UnmapCowDevices(LockedFile* lock, const std::string& name);

    // The reverse of MapPartitionWithSnapshot.
    bool UnmapPartitionWithSnapshot(LockedFile* lock, const std::string& target_partition_name);

    // Unmap a dm-user device through snapuserd.
    bool UnmapDmUserDevice(const std::string& dm_user_name);

    // Unmap a dm-user device for user space snapshots
    bool UnmapUserspaceSnapshotDevice(LockedFile* lock, const std::string& snapshot_name);

    // If there isn't a previous update, return true. |needs_merge| is set to false.
    // If there is a previous update but the device has not boot into it, tries to cancel the
    //   update and delete any snapshots. Return true if successful. |needs_merge| is set to false.
    // If there is a previous update and the device has boot into it, do nothing and return true.
    //   |needs_merge| is set to true.
    bool TryCancelUpdate(bool* needs_merge);

    // Helper for CreateUpdateSnapshots.
    // Creates all underlying images, COW partitions and snapshot files. Does not initialize them.
    Return CreateUpdateSnapshotsInternal(
            LockedFile* lock, const std::string& target_partition_name, uint64_t snapshot_size,
            PartitionCowCreator* cow_creator, AutoDeviceList* created_devices);

    // Initialize and map the snapshot.
    // Map the COW partition and zero-initialize the header.
    Return InitializeUpdateSnapshot(
            LockedFile* lock, Partition* target_partition,
            const LpMetadata* exported_target_metadata,
            const SnapshotStatus& snapshot_status);

    bool BackupSnapshot(LockedFile* lock, TemporaryFile& backup_file, const std::string& target_partition_name, uint64_t snapshot_size);
    bool RestoreSnapshot(LockedFile* lock, TemporaryFile& backup_file, const std::string& target_partition_name, uint64_t snapshot_size);

    // Implementation of UnmapAllSnapshots(), with the lock provided.
    bool UnmapAllSnapshots(LockedFile* lock);

    // Unmap all partitions that were mapped by CreateLogicalAndSnapshotPartitions.
    // This should only be called in recovery.
    bool UnmapAllPartitionsInRecovery();

    // Check no snapshot overflows. Note that this returns false negatives if the snapshot
    // overflows, then is remapped and not written afterwards.
    bool EnsureNoOverflowSnapshot(LockedFile* lock);

    enum class Slot { Unknown, Source, Target };
    friend std::ostream& operator<<(std::ostream& os, SnapshotManager::Slot slot);
    Slot GetCurrentSlot();

    // Return the suffix we expect snapshots to have.
    std::string GetSnapshotSlotSuffix();

    std::string ReadUpdateSourceSlotSuffix();

    // Helper for RemoveAllSnapshots.
    // Check whether |name| should be deleted as a snapshot name.
    bool ShouldDeleteSnapshot(const std::map<std::string, bool>& flashing_status, Slot current_slot,
                              const std::string& name);

    // Create or delete forward merge indicator given |wipe|. Iff wipe is scheduled,
    // allow forward merge on FDR.
    bool UpdateForwardMergeIndicator(bool wipe);

    // Helper for HandleImminentDataWipe.
    // Call ProcessUpdateState and handle states with special rules before data wipe. Specifically,
    // if |allow_forward_merge| and allow-forward-merge indicator exists, initiate merge if
    // necessary.
    UpdateState ProcessUpdateStateOnDataWipe(bool allow_forward_merge,
                                             const std::function<bool()>& callback);

    // Return device string of a mapped image, or if it is not available, the mapped image path.
    bool GetMappedImageDeviceStringOrPath(const std::string& device_name,
                                          std::string* device_string_or_mapped_path);

    // Same as above, but for paths only (no major:minor device strings).
    bool GetMappedImageDevicePath(const std::string& device_name, std::string* device_path);

    // Wait for a device to be created by ueventd (eg, its symlink or node to be populated).
    // This is needed for any code that uses device-mapper path in first-stage init. If
    // |timeout_ms| is empty or the given device is not a path, WaitForDevice immediately
    // returns true.
    bool WaitForDevice(const std::string& device, std::chrono::milliseconds timeout_ms);

    enum class InitTransition { SELINUX_DETACH, SECOND_STAGE };

    // Initiate the transition from first-stage to second-stage snapuserd. This
    // process involves re-creating the dm-user table entries for each device,
    // so that they connect to the new daemon. Once all new tables have been
    // activated, we ask the first-stage daemon to cleanly exit.
    //
    // If the mode is SELINUX_DETACH, snapuserd_argv must be non-null and will
    // be populated with a list of snapuserd arguments to pass to execve(). It
    // is otherwise ignored.
    bool PerformInitTransition(InitTransition transition,
                               std::vector<std::string>* snapuserd_argv = nullptr);

    SnapuserdClient* snapuserd_client() const { return snapuserd_client_.get(); }

    // Helper of UpdateUsesCompression
    bool UpdateUsesCompression(LockedFile* lock);
    // Locked and unlocked functions to test whether the current update uses
    // userspace snapshots.
    bool UpdateUsesUserSnapshots(LockedFile* lock);

    // Check if io_uring API's need to be used
    bool UpdateUsesIouring(LockedFile* lock);

    // Wrapper around libdm, with diagnostics.
    bool DeleteDeviceIfExists(const std::string& name,
                              const std::chrono::milliseconds& timeout_ms = {});

    android::dm::IDeviceMapper& dm_;
    std::unique_ptr<DeviceInfo> device_;
    std::string metadata_dir_;
    std::optional<std::pair<std::unique_ptr<Partition>, uint64_t>> pending_resize_;
    std::unique_ptr<IImageManager> images_;
    bool use_first_stage_snapuserd_ = false;
    bool in_factory_data_reset_ = false;
    std::function<bool(const std::string&)> uevent_regen_callback_;
    std::unique_ptr<SnapuserdClient> snapuserd_client_;
    std::unique_ptr<LpMetadata> old_partition_metadata_;
    std::optional<bool> is_snapshot_userspace_;
    MergeConsistencyChecker merge_consistency_checker_;
};

}  // namespace snapshot
}  // namespace capntrips

#ifdef DEFINED_FRIEND_TEST
#undef DEFINED_FRIEND_TEST
#undef FRIEND_TEST
#endif
