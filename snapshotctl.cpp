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

#include <libsnapshot/snapshot.h>

using namespace std::string_literals;

int Usage() {
    std::cerr << "snapshotctl: Control snapshots.\n"
                 "Usage: snapshotctl [action] [flags]\n"
                 "Actions:\n"
                 "  dump\n"
                 "    Print snapshot states.\n"
                 "  map\n"
                 "    Map all snapshots at /dev/block/mapper\n"
                 "  unmap\n"
                 "    Unmap all snapshots at /dev/block/mapper\n";
    return EX_USAGE;
}

namespace capntrips {
namespace snapshot {

bool DumpCmdHandler(int /*argc*/, char** argv) {
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

static std::map<std::string, std::function<bool(int, char**)>> kCmdMap = {
        // clang-format off
        {"dump", DumpCmdHandler},
        {"map", MapCmdHandler},
        {"unmap", UnmapCmdHandler},
        // clang-format on
};

}  // namespace snapshot
}  // namespace capntrips

int main(int argc, char** argv) {
    using namespace capntrips::snapshot;
    if (argc < 2) {
        return Usage();
    }

    for (const auto& cmd : kCmdMap) {
        if (cmd.first == argv[1]) {
            return cmd.second(argc, argv) ? EX_OK : EX_SOFTWARE;
        }
    }

    return Usage();
}
