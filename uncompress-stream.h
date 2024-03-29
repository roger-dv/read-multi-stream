/* uncompress-stream.h

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
#ifndef UNCOMPRESS_STREAM_H
#define UNCOMPRESS_STREAM_H

#include <string_view>

std::tuple<int, int> get_uncompressed_stream(std::string_view filepath);

#endif //UNCOMPRESS_STREAM_H
