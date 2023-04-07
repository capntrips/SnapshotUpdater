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

#include <string>

#ifdef LIBSNAPSHOT_USE_HAL
#include <android/hardware/boot/1.1/IBootControl.h>
#endif
#include <liblp/partition_opener.h>
#include <libsnapshot/snapshot.h>

// Forward declare IBootControl types since we cannot include only the headers
// with Soong. Note: keep the enum width in sync.
namespace android {
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

class DeviceInfo final {
    using MergeStatus = android::hardware::boot::V1_1::MergeStatus;
    using IImageManager = android::fiemap::IImageManager;

  public:
    std::string GetMetadataDir() const;
    std::string GetSlotSuffix() const;
    std::string GetOtherSlotSuffix() const;
    const android::fs_mgr::IPartitionOpener& GetPartitionOpener() const;
    std::string GetSuperDevice(uint32_t slot) const;
    bool IsOverlayfsSetup() const;
    bool SetBootControlMergeStatus(MergeStatus status);
    bool SetSlotAsUnbootable(unsigned int slot);
    bool IsRecovery() const;
    std::unique_ptr<IImageManager> OpenImageManager() const;
    std::unique_ptr<IImageManager> OpenImageManager(const std::string& gsid_dir) const;
    bool IsFirstStageInit() const;
    android::dm::IDeviceMapper& GetDeviceMapper();

    void set_first_stage_init(bool value) { first_stage_init_ = value; }

  private:
    bool EnsureBootHal();

    android::fs_mgr::PartitionOpener opener_;
    bool first_stage_init_ = false;
#ifdef LIBSNAPSHOT_USE_HAL
    android::sp<android::hardware::boot::V1_1::IBootControl> boot_control_;
#endif
};

}  // namespace snapshot
}  // namespace capntrips
