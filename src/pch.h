// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include <cstdint>
#include <climits>
#include <cmath>
#include <ctime>
#include <clocale>
#include <cassert>
#include <cctype>
#include <cwctype>
#include <cinttypes>

#include <bit>
#include <string>
#include <string_view>
#include <charconv>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <algorithm>
#include <queue>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <fstream>
#include <regex>
#include <numeric>
#include <any>
#include <iomanip>
#include <optional>
#include <span>

#include <thread>
#include <mutex>
//#include <condition_variable>

using namespace std::literals;

constexpr double M_PI = 3.141592653589793238463;
constexpr float M_PIF = 3.14159265358979f;
// std::numbers c++ 20

#include "util.h"
#include "util_geometry.h"
#include "util_strings.h"
#include "util_path.h"
#include "platform.h"
#include "util_date.h"
#include "util_selector.h"
#include "util_file.h"
#include "util_interfaces.h"
#include "util_json.h"
#include "util_map.h"

#include "app_icons.h"
#include "ui.h"
#include "app_settings.h"

extern const std::u8string_view s_app_name;
extern const std::u8string_view s_app_version;
extern const std::u8string_view g_app_build;
extern const wchar_t* s_app_name_l;
