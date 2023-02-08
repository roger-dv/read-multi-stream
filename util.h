/* util.h

Copyright 2018 Roger D. Voss

Created  by roger-dv on 04/21/2018.
Modified by roger-dv on 02/07/2023.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
#ifndef UTIL_H
#define UTIL_H

#include <cstdio>
#include <string_view>

std::string get_unmangled_name(std::string_view mangled_name);
int  get_file_desc(FILE *stream, int line_nbr);
bool valid_file(std::string_view filepath);
bool has_ending(std::string_view full_str, std::string_view ending, int &offset, int line_nbr);
bool dbg_echo_input_source(int fd, int line_nbr);
void dbg_dump_file_desc_flags(int fd);

#endif //UTIL_H
