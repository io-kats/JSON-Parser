# JSON Parser

A simple JSON parser.

## Table of contents
* [General info](#general-info)
* [Purpose](#purpose)
* [Features](#features)
* [Technologies](#technologies)
* [Setup](#setup)
* [Usage](#usage)
* [Project Status](#project-status)
* [Inspirations](#inspirations)
* [Credit](#credit)

## General info
Parser for JSON files. Assumes 
UTF-8 input. No dynamic allocations. Includes some non-standard values.

## Purpose
An exercise in hand-written parsing, wanting to 
have something of my own to use for serializing and deserializing data 
in human-readable form and wanting to see if I can make something 
using only the standard library.

## Features
An API for converting values:
- JSON-style strings to unicode codepoints with json_string_character_to
unicode_codepoint,
- JSON-style strings to UTF-8 with json_string_to_utf8, 
- getter functions for all types (using strtod for JSON-style floating 
point numbers).
- 2 extra token types for storing floating point numbers losslessly, 
namely their IEEE754 bit representation in base 16 (JSON_FLOAT_HEX
and JSON_DOUBLE_HEX), which can be read using hex_to_float/hex_to_double.

For more details, you can look at the header file.
	
## Technologies
Simple C++ with a little help from the C standard library, namely
- cstring for strlen, memcmp, memcpy and strerror
- cstdarg for va_start and va_end
- cstdint for fixed width types
- cstdlib for strtod (getting floats) and exit (debugging)
- cstdio for printf and fprintf (debugging)
- cerrno for errno (debugging)

## Setup

To run this project on Linux, go to wherever you cloned the repo and run (should work with g++ 9.3.0):
```
$ git clone https://github.com/io-kats/json-parser.git
$ cd ../json-parser
$ make
$ ./example.out
```
Similarly for Raspberry Pi 4 and raspbian.

For Windows:
- on MSVC, add the header file and test source file to a new VS project, define the `_CRT_SECURE_NO_WARNINGS` macro and run it 
(tested on Microsoft Visual Studio Community 2019 - Version 16.7.5) or
- compile with MinGW and g++:

```
$ git clone https://github.com/io-kats/json-parser.git
$ cd ../json-parser
$ g++ example.cpp -o example.exe
$ ./example.exe
```

## Usage
How to start:

Do this in at least on of your source files:

```
#define JSON_IMPLEMENTATION
#include "json.h"
```
This can be in any .cpp file, but it should be in only one of them.

A trivial JSON example:

```
// 0x4048f5c3 is hexadecimal for the 32-bit representation of 3.14
const char* json_string = 
"[\n"
"    null, \n"
"    {\n"
"        \"x\" : 1.5, \n"
"        \"y\": \n"
"        ["
"            \"\u0054\u0065\u0073\u0074\", \n"
"            0x4048f5c3 \n"   
"        ]\n"
"    },\n"
"    [\n"
"        1, \n"
"        -9223372036854775808\n"
"    ]\n"
"]\n";
```

Create a JSON node buffer:

```
size_t buf_size = 10;
ers::json::JsonNode* buf = new ers::json::JsonNode[buf_size];
```

Create parser and parse the JSON:

```
ers::json::JsonParser json_parser(json_string, strlen(json_string), &buf[0], buf_size);
json_parser.Parse();
```

If not enough space was allocated for the nodes:

```
while (json_parser.GetErrorCode() == JsonErrorCode::CAPACITY_EXCEEDED)
{
	delete[] buf;
	buf_size *= 2;	
	buf = new ers::json::JsonNode[buf_size];
	json_parser.Parse(&buf[0], buf_size);
}
```

You can inspect the nodes if you want:

```
ers::json::print_nodes(buf, buf_size);
```

The function by itself should output:

```
ARRAY: count = 3
NULL: null
OBJECT: count = 2
KEY: "x"
NUMBER: 1.5
KEY: "y"
ARRAY: count = 2
STRING: "\u0054\u0065\u0073\u0074"
FLOAT (HEX): 0x4048f5c3
ARRAY: count = 2
NUMBER: 1
NUMBER: -9223372036854775808
EOF: done!
```

print_nodes can be disabled by defining JSON_NDEBUG 
before including the header file.

You can also print out an error message if there's an error:

```
if (!json_parser.IsValid())
{
	printf("%s", json_parser.GetErrorMessage());
}
```
which could look like this:

```
Invalid token at line 2: null expected
...

     >>> nul, <<<
    {
        "x" : 1.5,
...
```

Getting a JSON string token and converting it to Unicode by using a 
path to it (syntax explained in header file):

```
const char* path1 = "[1].y[0]";
const ers::json::JsonNode* node1 = ers::json::get_value_node(buf, path1, strlen(path1));
assert(node1 != nullptr);
if (node1->type != ers::json::JsonNodeType::INVALID)
{
	const auto sv1 = node1->GetAsStringView();
	char buf[128] = { 0 }; 
	size_t buf_idx = 0; 
	size_t json_str_idx = 0;
	while (json_str_idx < sv1.length)
	{
		uint32_t cp = ers::json::util::json_string_character_to_codepoint(sv1.data, &json_str_idx);
		buf[buf_idx++] = cp;
	}
	buf[buf_idx] = 0;
	printf("root%s = %s\n", path1, &buf[0]); // prints: root[1].y[0] = "Test"
}
```

Getting a JSON string token and converting it to a UTF-8 encoded 
string:

```
const char* path2 = "[0]";
const ers::json::JsonNode* node2 = ers::json::get_value_node(buf, path2, strlen(path2));
assert(node2 != nullptr);
const auto sv2 = node2->GetAsStringView();

// The UTF-8 length of a string (max. 4 bytes) is always less than or equal 
// to that of its JSON-style equivalent (at least 1 more byte for escaped characters).
std::string str;		
str.resize(sv2.length); // Allocate enough space for the string.
size_t actual_length = ers::json::util::json_string_to_utf8(&str[0], sv2.data, sv2.length);
assert(actual_length != 0);
str.resize(actual_length);
printf("root%s = %s\n", path2, str.c_str()); // prints: root[0] = null
```

It is possible to convert nodes sequentially, by traversing an array/object:

```
const char* path3 = "[1].y"; // this is an array
const ers::json::JsonNode* node3 = ers::json::get_value_node(buf, path3, strlen(path3));
assert(node3 != nullptr);
const ers::json::JsonNode* curr = node3->GetFirst();
assert(curr != nullptr);
str.resize(255);
actual_length = curr->GetAsString(&str[0]);
str.resize(actual_length);
printf("root%s[0] = %s\n", path3, str.c_str()); // prints: root[1].y[0] = Test

curr = curr->GetNext();
assert(curr);
float y1;
int rc = curr->GetAsFloat(&y1);
assert(rc == 1);
printf("root%s[1] = %.9g\n", path3, y1); // prints: root[1].y[1] = 3.1400001
```

It is also possible to convert JSON numbers into integers:

```
int64_t n;
const char* path4 = "[2][-1]";
const ers::json::JsonNode* node4 = ers::json::get_value_node(buf, path4, strlen(path4));
rc = node4->GetAsS64(&n);
assert(rc == 1);
printf("root%s = %zd\n", path4, n); // prints: root[2][-1] = 9223372036854775807
```

There's more examples in the example.cpp file, including how to use the FlatJson class,
so definitely take a look at it.

Defining JSON_NDEBUG before the header file deactivates all 
debugging facilities, including some methods:

```
#define JSON_NDEBUG
#define JSON_IMPLEMENTATION
#include "json.h"
```

and strtod can be replaced 
with your own implementation by defining the macro JSON_STRTOF(dest, begin, end),
for example if you want to use charconv's "from_chars":

```
#include <cassert>
#include <charconv>
template <typename T>
void my_strtod(T* dest, const char* begin, const char* end)
{
	std::from_chars_result result = std::from_chars(begin, end, *dest);
	assert(((end) == result.ptr) && (result.ec == std::errc()) && "Failed to parse float.");
}

#define JSON_STRTOD(dest, begin, end) my_strtod(dest, begin, end)
#define JSON_IMPLEMENTATION
#include "json.h"
```
or you can completely disable it, if you don't need to read in any numbers (bar the 2 extra hex types):
```
#define JSON_NO_FLOAT
#define JSON_IMPLEMENTATION
#include "json.h"
```

For more details, take a look at the header file. The examples are all
in the example.cpp file and you can run it as explained in [Setup](#setup).

## Project Status
The project is finished.

## Inspirations
- Sergey Lyubka's JSON Parser "Frozen" API: https://github.com/cesanta/frozen
- The OpenGEX format, specifically for non-binary lossless floating point 
number storage: http://opengex.org/comparison.html
- Sean Barrett's STB single header file libraries: https://github.com/nothings/stb/

## Credit
- Rita Łyczywek's readme tutorial and template:
https://bulldogjob.com/news/449-how-to-write-a-good-readme-for-your-github-project
https://github.com/ritaly/README-cheatsheet
- The test002.json file was retrieved from this Wikipedia page on 30/04/2022: https://de.wikipedia.org/wiki/JavaScript_Object_Notation#Beispiel
