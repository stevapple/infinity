// Copyright(C) 2023 InfiniFlow, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
module;

export module config;

import stl;
import third_party;
import options;
import status;
import command_statement;

namespace infinity {

export constexpr std::string_view profile_history_capacity_name = "profile_history_capacity";
export constexpr std::string_view enable_profile_name = "enable_profile";
export constexpr std::string_view worker_cpu_limit = "cpu_count";
export constexpr std::string_view log_level = "log_level";

export struct DefaultConfig {
    LogLevel default_log_level_{LogLevel::kInfo};
    bool default_log_to_stdout_{false};
};

export struct Config {
public:
    Status Init(const SharedPtr<String> &config_path, DefaultConfig* default_config);

    void PrintAll();

    // General
    String Version();
    String TimeZone();
    i64 TimeZoneBias();

    void SetCPULimit(i64 new_cpu_limit);
    i64 CPULimit();
    inline bool RecordRunningQuery() {
        return record_running_query_;
    }
    void SetRecordRunningQuery(bool flag);

    // Network
    String ServerAddress();
    i64 PostgresPort();
    i64 HTTPPort();
    i64 ClientPort();
    i64 ConnectionPoolSize();

    // Log
    String LogFileName();
    String LogDir();
    String LogFilePath();
    bool LogToStdout();
    i64 LogFileMaxSize();
    i64 LogFileRotateCount();

    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel();

    // Storage
    String DataDir();

    i64 CleanupInterval();

    i64 CompactInterval();

    i64 OptimizeIndexInterval();

    i64 MemIndexCapacity();

    // Buffer
    i64 BufferManagerSize();
    SizeT LRUNum();
    String TempDir();

    // WAL
    String WALDir();

    i64 WALCompactThreshold();

    i64 FullCheckpointInterval();

    i64 DeltaCheckpointInterval();

    i64 DeltaCheckpointThreshold();

    FlushOptionType FlushMethodAtCommit();

    // Resource
    String ResourcePath();

public:
    // Get config by name
    Tuple<BaseOption *, Status> GetConfigByName(const String& name);

    GlobalOptionIndex GetOptionIndex(const String& var_name) const { return global_options_.GetOptionIndex(var_name); }
private:
    static void ParseTimeZoneStr(const String &time_zone_str, String &parsed_time_zone, i32 &parsed_time_zone_bias);

    static Status ParseByteSize(const String &byte_size_str, i64 &byte_size);

    static Status ParseTimeInfo(const String &time_info, i64 &time_seconds);

    static u64 GetAvailableMem();

private:
    std::mutex mutex_;
    GlobalOptions global_options_;

    // record running query flag
    Atomic<bool> record_running_query_{false};
};

} // namespace infinity
