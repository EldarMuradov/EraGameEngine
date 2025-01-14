// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core/threading.h"
#include "core/profiling_internal.h"

namespace era_engine 
{
	extern bool cpuProfilerWindowOpen;
}

#if ENABLE_CPU_PROFILING

#define _CPU_PROFILE_BLOCK_(counter, name) era_engine::cpu_profile_block_recorder COMPOSITE_VARNAME(__PROFILE_BLOCK, counter)(name)
#define CPU_PROFILE_BLOCK(name) _CPU_PROFILE_BLOCK_(__COUNTER__, name)

#define MAX_NUM_CPU_PROFILE_BLOCKS 16384
#define MAX_NUM_CPU_PROFILE_EVENTS (MAX_NUM_CPU_PROFILE_BLOCKS * 2)
#define MAX_NUM_CPU_PROFILE_STATS 512

// 1 bit for array index, 1 free bit, 20 bits for events, 10 bits for stats.
#define _CPU_PROFILE_GET_ARRAY_INDEX(v) ((v) >> 31)
#define _CPU_PROFILE_GET_EVENT_INDEX(v) ((v) & 0xFFFFF)
#define _CPU_PROFILE_GET_STAT_INDEX(v)	(((v) >> 20) & 0x3FF)

#define recordProfileEvent(type_, name_) \
	uint32 arrayAndEventIndex = cpuProfileIndex++; \
	uint32 eventIndex = _CPU_PROFILE_GET_EVENT_INDEX(arrayAndEventIndex); \
	uint32 arrayIndex = _CPU_PROFILE_GET_ARRAY_INDEX(arrayAndEventIndex); \
	ASSERT(eventIndex < MAX_NUM_CPU_PROFILE_EVENTS); \
	profile_event* e = cpuProfileEvents[arrayIndex] + eventIndex; \
	e->threadID = getThreadIDFast(); \
	e->name = name_; \
	e->type = type_; \
	QueryPerformanceCounter((LARGE_INTEGER*)&e->timestamp); \
	cpuProfileCompletelyWritten[arrayIndex].fetch_add(1, std::memory_order_release); // Mark this event as written. Release means that the compiler may not reorder the previous writes after this.

#define _CPU_PROFILE_STAT(labelValue, value, member, valueType) \
	uint32 arrayAndStatIndex = cpuProfileIndex.fetch_add(1 << 20); \
	uint32 statIndex = _CPU_PROFILE_GET_STAT_INDEX(arrayAndStatIndex); \
	uint32 arrayIndex = _CPU_PROFILE_GET_ARRAY_INDEX(arrayAndStatIndex); \
	ASSERT(statIndex < MAX_NUM_CPU_PROFILE_STATS); \
	profile_stat* stat_ = cpuProfileStats[arrayIndex] + statIndex; \
	stat_->label = labelValue; \
	stat_->member = value; \
	stat_->type = valueType; \
	cpuProfileCompletelyWritten[arrayIndex].fetch_add(1, std::memory_order_release); // Mark this stat as written. Release means that the compiler may not reorder the previous writes after this.

//#undef recordProfileEvent

#define _CPU_PRINT_PROFILE_BLOCK_(counter, name) cpu_print_profile_block_recorder COMPOSITE_VARNAME(__PROFILE_BLOCK, counter)(name)
#define CPU_PRINT_PROFILE_BLOCK(name) _CPU_PRINT_PROFILE_BLOCK_(__COUNTER__, name)

namespace era_engine
{
	enum profile_stat_type
	{
		profile_stat_type_bool,
		profile_stat_type_int32,
		profile_stat_type_uint32,
		profile_stat_type_int64,
		profile_stat_type_uint64,
		profile_stat_type_float,
		profile_stat_type_string,
	};

	struct profile_stat
	{
		const char* label;
		union
		{
			bool boolValue;
			int32 int32Value;
			uint32 uint32Value;
			int64 int64Value;
			uint64 uint64Value;
			float floatValue;
			const char* stringValue;
		};
		profile_stat_type type;
	};

	extern std::atomic<uint32> cpuProfileIndex;
	extern std::atomic<uint32> cpuProfileCompletelyWritten[2];
	extern profile_event cpuProfileEvents[2][MAX_NUM_CPU_PROFILE_EVENTS];
	extern profile_stat cpuProfileStats[2][MAX_NUM_CPU_PROFILE_STATS];

	struct cpu_profile_block_recorder
	{
		cpu_profile_block_recorder(const char* name)
			: name(name)
		{
			recordProfileEvent(profile_event_begin_block, name);
		}

		~cpu_profile_block_recorder()
		{
			recordProfileEvent(profile_event_end_block, name);
		}

		const char* name;
	};

	inline void cpuProfilingFrameEndMarker()
	{
		recordProfileEvent(profile_event_frame_marker, 0);
	}

	inline void CPU_PROFILE_STAT(const char* label, bool value) { _CPU_PROFILE_STAT(label, value, boolValue, profile_stat_type_bool); }
	inline void CPU_PROFILE_STAT(const char* label, int32 value) { _CPU_PROFILE_STAT(label, value, int32Value, profile_stat_type_int32); }
	inline void CPU_PROFILE_STAT(const char* label, uint32 value) { _CPU_PROFILE_STAT(label, value, uint32Value, profile_stat_type_uint32); }
	inline void CPU_PROFILE_STAT(const char* label, int64 value) { _CPU_PROFILE_STAT(label, value, int64Value, profile_stat_type_int64); }
	inline void CPU_PROFILE_STAT(const char* label, uint64 value) { _CPU_PROFILE_STAT(label, value, uint64Value, profile_stat_type_uint64); }
	inline void CPU_PROFILE_STAT(const char* label, float value) { _CPU_PROFILE_STAT(label, value, floatValue, profile_stat_type_float); }
	inline void CPU_PROFILE_STAT(const char* label, const char* value) { _CPU_PROFILE_STAT(label, value, stringValue, profile_stat_type_string); }

	void cpuProfilingResolveTimeStamps();

	struct cpu_print_profile_block_recorder
	{
		cpu_print_profile_block_recorder(const char* name)
			: name(name)
		{
			QueryPerformanceCounter((LARGE_INTEGER*)&start);
		}

		~cpu_print_profile_block_recorder()
		{
			uint64 end;
			QueryPerformanceCounter((LARGE_INTEGER*)&end);

			uint64 clockFrequency;
			QueryPerformanceFrequency((LARGE_INTEGER*)&clockFrequency);

			float duration = (float)(end - start) / clockFrequency * 1000.f;
			std::cout << "Profile block '" << name << "' took " << duration << "ms.\n";
		}

		const char* name;
		uint64 start;
	};
}

#else

#define CPU_PROFILE_BLOCK(...)
#define CPU_PROFILE_STAT(...)

#define cpuProfilingFrameEndMarker(...)
#define cpuProfilingResolveTimeStamps(...)

#define CPU_PRINT_PROFILE_BLOCK(...)

#endif