//
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
//

#include <sysexits.h>

#include <chrono>
#include <iostream>
#include <map>
#include <sstream>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <fs_mgr.h>

#include <libsnapshot/snapshot.h>
#include "version.hpp"

using namespace std::string_literals;

namespace capntrips {
namespace snapshot {

int Usage(int, char** argv) {
    std::cerr << "SnapshotUpdater " << version << std::endl
              << "Usage: " << argv[0] << " [action] [flags]" << std::endl
              << "Actions" << std::endl
              << "  dump" << std::endl
              << "    Print snapshot states." << std::endl
              << "  map" << std::endl
              << "    Map all snapshots" << std::endl
              << "  unmap" << std::endl
              << "    Unmap all snapshots" << std::endl
              << "  update <snapshot-name> <snapshot-size>" << std::endl
              << "    Update snapshot" << std::endl;
    return EX_USAGE;
}

bool DumpCmdHandler(int, char** argv) {
    android::base::InitLogging(argv, &android::base::StderrLogger);
    return SnapshotManager::New()->Dump(std::cout);
}

bool MapCmdHandler(int, char** argv) {
    android::base::InitLogging(argv, &android::base::StderrLogger);
    using namespace std::chrono_literals;
    return SnapshotManager::New()->MapAllSnapshots(5000ms);
}

bool UnmapCmdHandler(int, char** argv) {
    android::base::InitLogging(argv, &android::base::StderrLogger);
    return SnapshotManager::New()->UnmapAllSnapshots();
}

bool UpdateCmdHandler(int argc, char** argv) {
    if(argc != 4) {
        std::cerr <<  "Usage: " << argv[0] << " update <partition-name> <snapshot-size>" << std::endl;
        return EX_USAGE;
    }
    auto partition_name = argv[2];
    auto target_partition_name = partition_name + fs_mgr_get_other_slot_suffix();
    auto partition_size = strtoull(argv[3], nullptr, 0);
    return SnapshotManager::New()->CreateUpdateSnapshots(target_partition_name, partition_size);
}

bool VersionCmdHandler(int, char** argv) {
    android::base::InitLogging(argv, &android::base::StdioLogger);
    LOG(INFO) << "SnapshotUpdater " << version;
    return true;
}

static std::map<std::string, std::function<bool(int, char**)>> kCmdMap = {
        // clang-format off
        {"dump", DumpCmdHandler},
        {"map", MapCmdHandler},
        {"unmap", UnmapCmdHandler},
        {"update", UpdateCmdHandler},
        {"-v", VersionCmdHandler},
        {"--version", VersionCmdHandler},
        // clang-format on
};

}  // namespace snapshot
}  // namespace capntrips

int main(int argc, char** argv) {
    using namespace capntrips::snapshot;
    if (argc < 2) {
        return Usage(argc, argv);
    }

    for (const auto& cmd : kCmdMap) {
        if (cmd.first == argv[1]) {
            return cmd.second(argc, argv) ? EX_OK : EX_SOFTWARE;
        }
    }

    return Usage(argc, argv);
}
