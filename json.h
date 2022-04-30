/*
Copyright 2021 Ioannis Katsios <ioannis.katsios1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef JSON_INCLUDE_H
#define JSON_INCLUDE_H

#include <cstring> // strerror, memcpy, memcmp, strlen
#include <cstdarg> // va_start, va_end
#include <cerrno> // errno
#include <cstdio> // printf, fprintf, stderr
#include <cstdlib> // exit
#include <cstdint> // uint8_t, uint32_t

namespace ers
{
	namespace json 
	{
		/**
		 * JSON parser for parsing of JSON files. Assumes UTF-8 input.
		 * Lightly inspired by Sergey Lyubka's JSON Parser "Frozen" API: https://github.com/cesanta/frozen
		 * and also by the OpenGEX format (specifically for non-binary lossless float storage): http://opengex.org/comparison.html
		 * An exercise in manual parsing, wanting to have something
		 * of my own to use for serializing and deserializing data in human-readable form
		 * and wanting to see if I can make something without many dependencies.
		 *
		 * It creates a structure where all JSON keys and values (JSON nodes) are accessible sequentially and as close to
		 * the relevant nodes, i.e. it is flat, items in a JSON are next to each other in memory and so are properties 
		 * in a JSON object, unless the value itself is an array/object. 
		 * For that reason, every value in a JSON array is linked to the next value, creating a singly linked list for values. 
		 * The same goes for keys in a JSON object, as well as the values associated with those keys.
		 * This way, keys and values can be independently forward-iterated.
		 * A JSON object would look like this, first to last:
		 * 
		 * {
		 *   KEY -> VALUE
		 * 	  |       |
		 *   KEY -> VALUE
		 * 	  |       |
		 *   KEY -> VALUE
		 * 	  |       |
		 *   KEY -> VALUE
		 * 	  |       |
		 * 	  .       .
		 * 	  .       .
		 * 	  .       .
		 * } 
		 * 
		 * and a JSON array like this:
		 * 
		 * [
		 *   VALUE
		 * 	   |
		 *   VALUE
		 * 	   |
		 *   VALUE
		 * 	   |
		 *   VALUE
		 *     |
		 * 	   .
		 * 	   .
		 * 	   .
		 * ] 
		 * 
		 * Keys and their associated values are also right next to each other in memory.
		 *
		 * There is an API for converting some values (these functions assume correctness of the nodes):
		 * - JSON-style strings to unicode codepoints with json_string_character_to_codepoint, and
		 * - JSON-style strings to UTF-8 with json_string_to_utf8, 
		 * - single/double-precision ieee754 floats to floats/doubles with hex_to_float/hex_to_double.
		 * - getter functions for all types (using strtod for JSON-style floating point numbers).
		 *
		 * Note: 
		 * - The parser is non-owning. The user supplies the parser with a buffer of JsonNodes, as well as its capacity.
		 * A helper class called FlatJson that manages a statically allocated JsonNode buffer is supplied to help with that,
		 * but is not hard-coded into the parser.
		 * - It may be under the "ers" namespace that I use, it is however not dependent on it. Change it if you want to.
		 * 
		 *  Working examples and test in example.cpp.
		 */

		using u8 = uint8_t;
		using u32 = uint32_t;
		using u64 = uint64_t;
		using s64 = int64_t;
		using f32 = float;
		using f64 = double;
		using size_type = size_t;

		enum class JsonTokenType
		{
			INVALID = 0, // Invalid token found during tokenization.

			// 6 structural tokens
			JSON_ARRAY_BEGIN, 
			JSON_OBJECT_BEGIN,
			JSON_ARRAY_END,
			JSON_OBJECT_END,
			JSON_COLON,
			JSON_COMMA,

			// 3 literal tokens
			JSON_TRUE, 
			JSON_FALSE,
			JSON_NULL,

			// 2 value tokens
			JSON_NUMBER, 
			JSON_STRING,

			// Two extra value types, non standard for JSON.
			JSON_FLOAT_HEX, // single-precision ieee754 fp bits for lossless storage: e.g. 0x89ABCDEF (always 8 characters)
			JSON_DOUBLE_HEX, // same as above, but for double-precision: e.g. 0x0123456789ABCDEF (always 16 characters)

			// Helper token types.
			JSON_KEY, // Useful for getting a specific value. Assigned after tokenization. A key is also a string.
			JSON_EOF, // End of file.
			
			SYNTACTIC_ERROR // Token at which parsing fails.
		};

		struct JsonToken
		{
			const char* data; // Pointer to the beginning of the token.
			size_type length; // Stores the length of the token.
			JsonTokenType type; // Type of the token.
		};

		enum class JsonNodeType
		{
			INVALID = 0,
			JSON_ARRAY, 
			JSON_OBJECT,
			JSON_TRUE, 
			JSON_FALSE,
			JSON_NULL,
			JSON_NUMBER, 
			JSON_FLOAT_HEX,
			JSON_DOUBLE_HEX,
			JSON_STRING,
			JSON_KEY, 
			JSON_EOF,			
			SYNTACTIC_ERROR
		};

		struct string_view
		{
			const char* data;
			size_type length;
		};

		struct JsonNode
		{		
			union 
			{
				string_view as_sv; // points to the string in the JSON.
				size_type count; // count of items/properties in a JSON array/object
			};			
			JsonNodeType type; // Type of the node.

			/**
			 * Points to the next value in arrays or key in objects.
			 * For values associated with keys and structural tokens (bar ARRAY_BEGIN/OBJECT_BEGIN), 
			 * next == nullptr.
			 * Implementation subtlety/detail: if the token is an array/object and an element of an array, 
			 * 'next' points to the next value in the array. If it is the value to a key in an object, 
			 * then 'next' points to the token's own closing bracket.
			 */
			JsonNode* next;

			JsonNode() = default;
			JsonNode(const JsonToken& token);

			/**
			 * Returns a pointer to the first item in an object/array or nullptr if empty/not an object/array.
			 */
			const JsonNode* GetFirst() const;

			/**
			 * If the token is a JSON_KEY, it return the associated value otherwise it return nullptr.
			 */
			const JsonNode* GetValue() const;

			/**
			 * Returns the next logical token.
			 * 
			 * @return next item in an array, next key in an object if the token is a key, 
			 * or value of next property in an object if the token is a value.
			 */
			const JsonNode* GetNext() const;

			/** 
			 * Getters for the value from a given value token. Assumes enough allocated space.
			 * Returns 0 for failure (or t == nullptr) and:
			 * - bool: 1 if succesfully read a boolean (true/false).
			 * - float: 1 if succesfully read a floating point number from a hexadecimal bit representation
			 * - double: 1 if succesfully read a double-precision floating point number 
			 * from either a JSON-style number (with strtod) or a hexadecimal bit representation
			 * - u64/s64: 1 if succesfully read a 64-bit integer (unsigned/signed)			 
			 * - char*: the number of bytes written from a JSON-style string or anyother value, except for arrays and objects. 
			 * NOTE: I suggest using the get_value_node function/GetValueNode method for the char* case, as polling for just the size
			 * to allocate it later will lead to searching for the token twice.
			 * @param dest pointer to value to be written to. If dest == nullptr,
			 * we just get the the number of bytes that would have been written to it.
			 * @return return code as described above.
			 */
			int GetAsBool(bool* dest) const;
			int GetAsFloat(f32* dest) const;
			int GetAsDouble(f64* dest) const;
			int GetAsU64(u64* dest) const;
			int GetAsS64(s64* dest) const;
			size_type GetAsString(char* dest) const;

			/**
			 * Return the string view for the token in the JSON. 
			 */
			string_view GetAsStringView() const;

			/**
			 * Return array/object item count. 
			 */
			size_type GetCount() const;

			/**
			 * Return the a boolean depending on the type of the node. 
			 */
			bool IsKey() const;
			bool IsValue() const;
			bool IsComplex() const;
			bool IsInvalid() const;
			bool IsNumber() const;
			bool IsBool() const;
			bool IsString() const;
			bool IsNull() const;
			bool IsArray() const;
			bool IsObject() const;
			bool IsEOF() const;
		};

		/**
		 * Get the node of value given some path.
		 * Returns an null node if not found.
		 * 
		 * @param path the path to be followed.
		 * @param node_buffer pointer to node buffer.
		 * @param node_count count of nodes in buffer.
		 * @return pointer the node the path leads too, nullptr if it doesn't exist
		 * or the path is malformed.
		 * 
		 * Path syntax:
		 * 1) . -> denotes a new object
		 * 2) [x] -> denotes [x+1]-th element of an array, e.g. [0] would denote the 1st element.
		 * 3) "key" or key -> denotes the key of an object (JSON-style string).
		 * e.g. for a JSON file: ["Harry", {"x" : 1.5, "y" : [2, 3.14]}], 
		 * a path "[0]" returns the token for "Harry", whereas [1].y[1] returns the token for 3.14.
		 * NOTE: The maximum allowed absolute value of an index is 2^64 - 1.
		 * NOTE: If the index is greater than the item/pair count of an array/object, it wraps around,
		 * so it's never invalid, unless it's not an integer.
		 * NOTE: Indices are allowed to be negative, so [-1] would denote the last item in an array/object.
		 * Negative indices also wrap around.
		 */
		const JsonNode* get_value_node(const JsonNode* node_buffer, size_type node_count, const char* path, size_type path_string_length);

		enum class JsonErrorCode
		{
			NOT_DONE = 0,
			VALID_JSON,
			INVALID_TOKENS,
			SYNTACTIC_ERRORS,
			CAPACITY_EXCEEDED
		};

		/** 
		 * Struct for more easily and statically defining a JsonNode buffer to parse into,
		 * as well as doing useful stuff with it.
		 */
		template <size_type NODE_CAPACITY>
		class FlatJson
		{
		public:
			friend class JsonParser;

			FlatJson();

			JsonNode& operator[] (size_t i);
			const JsonNode& operator[] (size_t i) const;

			JsonNode& GetBegin ();
			const JsonNode& GetBegin() const;

			/** 
			 * Wrapper method for get_value_node, see above.
			 */
			const JsonNode* GetValueNode(const char* path, const JsonNode* node = nullptr) const;

			/** 
			 * Getters for the value from a given value token. Assumes enough allocated space.
			 * Returns 0 for failure (or t == nullptr) and:
			 * - bool: 1 if succesfully read a boolean (true/false).
			 * - float: 1 if succesfully read a floating point number from a hexadecimal bit representation
			 * - double: 1 if succesfully read a double-precision floating point number 
			 * from either a JSON-style number (with strtod) or a hexadecimal bit representation
			 * - u64/s64: 1 if succesfully read a 64-bit integer (unsigned/signed)			 
			 * - char*: the number of bytes written from a JSON-style string or anyother value, except for arrays and objects. If dest == nullptr,
			 * we just get the the number of bytes that would have been written to it.
			 * NOTE: I suggest using the get_value_node function/GetValueNode method for the char* case, as polling for just the size
			 * to allocate it later will lead to searching for the token twice.
			 * @param path the path to be followed. Syntax is the same as in get_value_node.
			 * @param dest pointer to value to be written to.
			 * @param node (optional) pointer to json object or json array to search for the value,
			 *		which can speed up searching for values and shorten path lengths.
			 */
			int GetAsBool(const char* path, bool* dest, const JsonNode* node = nullptr) const;
			int GetAsFloat(const char* path, f32* dest, const JsonNode* node = nullptr) const;
			int GetAsDouble(const char* path, f64* dest, const JsonNode* node = nullptr) const;
			int GetAsU64(const char* path, u64* dest, const JsonNode* node = nullptr) const;
			int GetAsS64(const char* path, s64* dest, const JsonNode* node = nullptr) const;
			size_type GetAsString(const char* path, char* dest, const JsonNode* node = nullptr) const;

			/**
			 * Returns the token count.
			 * 
			 * @return the count of node in the node buffer.
			 */
			size_type GetCount() const;

			/**
			 * Returns the token buffer capacity.
			 * 
			 * @return the capacity of the node buffer.
			 */
			size_type GetCapacity() const;

		private:

			JsonNode m_nodes[NODE_CAPACITY];
			size_type m_count;
		};

		constexpr size_type JSON_ERROR_MESSAGE_LENGTH = 255;
		class JsonParser
		{
		public:

			JsonParser() = default;

			/**
			 * Pass in a pointer to the base of a JsonToken buffer with a certain alloted capacity
			 * as well as a pointer to the JSON text with its length.
			 * Passing a zero capacity will just count JSON token, but won't store any of them.
			 * It will, however calculate them anyway.
			 *
			 * @param data_ pointer to buffer containing JSON file.
			 * @param length_ length of buffer containing JSON file.
			 * @param buffer pointer to token buffer.
			 * @param capacity capacity of token buffer.			 
			 */
			JsonParser(const char* data_, size_type length_, JsonNode* buffer = nullptr, size_type capacity = 0);

			/**
			 * Same as above, the non-default constructor is a wrapper over this.		 
			 */
			void SetUp(const char* data_, size_type length_, JsonNode* buffer = nullptr, size_type capacity = 0);

			/** 
			 * Parses the JSON file into a JsonNode buffer.
			 * 
			 * @param new_buffer new pointer to buffer, to be used if the original capacity was exceeded (check error code).
			 * @param new_capacity new buffer capacity, see above.
			 */
			void Parse(JsonNode* new_buffer = nullptr, size_type new_capacity = 0);

			/** 
			 * Parses the JSON file into a FlatJson instance.
			 * 
			 * @param flat_json FlatJson struct to parse the JSON into.
			 */
			template <size_type NODE_CAPACITY>
			void Parse(FlatJson<NODE_CAPACITY>& flat_json);
			
			/**
			 * Returns true if tokenization and parsing were successful.
			 * 
			 * @return true if tokenization and parsing were successful, false otherwise.
			 */
			bool IsValid() const;

			/**
			 * Returns the token count.
			 * 
			 * @return the count of node in the node buffer.
			 */
			size_type GetCount() const;

			/**
			 * Returns the token buffer capacity.
			 * 
			 * @return the capacity of the node buffer.
			 */
			size_type GetCapacity() const;

			/**
			 * Returns the error code.
			 * 			 
			 * @return the error code.
			 */
			JsonErrorCode GetErrorCode() const;

			/**
			 * Returns the error code.
			 * 			 
			 * @return pointer to the error message.
			 */
			const char* GetErrorMessage() const;

		private:

			/**
			 * Parsers for JSON arrays and objects used to recursively parse the file.
			 * 
			 * @return true if parsing was successful, false otherwise.
			 */
			bool parseArray();
			bool parseObject();

			/**
			 * Checks if the token is the expected one, if not, it finds what sort of error there is.
			 * 
			 * @param token the token being checked.
			 * @param expected the boolean expression that determines whether the token is the one we are expecting during parsing.
			 * @param message pointer to the error message for the expected token.
			 * @return the value of 'expected'.
			 */
			inline bool expect(bool expected, const char* message = nullptr);

			/** Pushes a new node into the node buffer.
			 * 
			 * @param node the node to be pushed.
			 * @return false if capacity exceeded, true otherwise.
			 */
			bool pushNode();

			/**
			 * Error logging internal function like printf.
			 * 
			 * @param fmt format string.
			 */
			void appendToErrorLog(const char* fmt, ...);

			/**
			 * Error logging internal function for invalid tokens.
			 * 
			 * @param message extramessage to append.
			 */
			void logInvalidTokenError(JsonTokenType actual_type, const char* message = nullptr);	

			/**
			 * Error logging internal function for the position of the invalid token in the JSON itself
			 * to help with finding and fixing it.
			 */
			void logInvalidTokenPosition();

			/** Returns pointer to last pushed node.
			 * 
			 * @return pointer to last pushed node.
			 */
			JsonNode* getLastNode();

			/**
			 * 
			 * Calculates and returns the next token.
			 * 
			 * @return the next token.
			 */
			JsonToken getNextToken();

			/**
			 * Matching functions for producing tokens.
			 */
			bool matchString(); // For JSON-style strings.
			bool matchNumber(); // For JSON-style numbers.
			bool matchLiteral(const char* s, u8 len);  // For true, false and null.
			int matchFloatHex(); // Return value: 0 - false, 1 - float, 2 - double

			/**
			 * Internal helpers.
			 */
			void skipWhitespace();
			bool isDigit(char ch) const;
			bool isHexDigit(char ch) const;
			bool isWhitespace(char ch) const;
			bool isStructural(char ch) const;
			bool isValid(char ch) const;
			bool isPrimitiveValueToken(JsonToken* t) const;

		private:
			const char* m_begin; // Points to start of JSON file.
			const char* m_pos; // Points to current position in JSON file.
			const char* m_end; // Points to end of JSON file.
			JsonErrorCode m_errorCode; // Stores the state of tokenization/parsing.

			JsonNode* m_nodeBuffer;
			size_type m_nodeCount;
			size_type m_tokenCapacity;

			JsonToken m_currentToken;
			size_type m_currentLine;
			char m_errorLog[JSON_ERROR_MESSAGE_LENGTH + 1];
			char* m_errorLogPos;
		};	

		namespace util 
		{
			/**
			 * Prints all JSON node from a buffer. If JSON_NDEBUG is defined, the function does nothing.
			 */
			void print_nodes(const JsonNode* node_buffer, size_type node_count);

			/** 
			 * Print a JSON node. If JSON_NDEBUG is defined, the function does nothing.
			 * 
			 * @param t pointer to node to be printed.
			 */
			void print_node(const JsonNode* t);

			/**
			 * Returns length of a UTF-8 byte sequence.
			 * 
			 * @param s first byte of a UTF-8 byte sequence.
			 */
			size_type utf8_len(char ch);

			/**
			 * Returns the closest codepoint for the character point at by str + *idx.
			 * 
			 * @param s pointer to base of string to convert.
			 * @param idx index to the string pointed to by str.
			 * 
			 * The function assumes the JSON string is parsed already and is thus valid.
			 * Example: for a JSON file like ["Harry", {"x" : 1.5, "y" : ["te\u0073t", 3.14]}]
			 * the following code should print "test":
			 * 
			 * const char* jsonFile = "[\"Harry\", {\"x\" : 1.5, \"y\" : [\"te\\u0073t\", 3.14]}]";
			 * .....
			 * .....
			 * const char* path = "[1].y[0]";
			 * ers::JsonToken* t = json->GetValueNode(path, strlen(path));
			 * char buf[64] = { 0 }; size_type i = 0; size_type idx = 0;
			 * while (idx < t->length)
			 * {
			 * 	u32 cp = ers::JsonParser::json_string_character_to_codepoint(t->data, &idx);
			 * 	buf[i++] = cp;
			 * }
			 * printf("%s\n", buf);
			 */
			u32 json_string_character_to_codepoint(const char* s, size_type* idx);

			/**
			 * Returns the number of bytes written to dest. If dest is nullptr, it return the number
			 * of bytes that would have been written to it.
			 * 
			 * @param dest pointer to base of string to be written to.
			 * @param src pointer to base of JSON-style string.
			 * @param length length of src string.
			 * 
			 * Example: for a JSON file like ["Harry", {"x" : 1.5, "y" : ["te\u0073t", 3.14]}]
			 * the following code should print "test" (including quotation marks):
			 * 
			 * const char* jsonFile = "[\"Harry\", {\"x\" : 1.5, \"y\" : [\"te\\u0073t\", 3.14]}]";
			 * .....
			 * .....
			 * const char* path = "[1].y[0]";
			 * ers::JsonToken* t = json.GetValueNode(path, strlen(path));
			 * char buf[64] = { 0 }; size_type new_length = 0;
			 * if (t->type != ers::JsonTokenType::INVALID)
			 *  new_length = ers::JsonParser::json_string_to_utf8(&buf[0], t->data, t->length));
			 * buf[new_length] = 0;
			 * printf("%s\n", buf);
			 */
			size_type json_string_to_utf8(char* dest, const char* src, size_type length);

			/**
			 * Helper function. Returns the value of a hex digit character.
			 * 
			 * @param ch the hex digit character.
			 */
			u32 hex_digit_to_u32(char ch);

			/**
			 * Returns a floating point number from its bit representation in hexadecimal (ieee754).
			 * 
			 * @param s pointer to base of string of the bit representation in hexadecimal (ieee754).
			 */
			f32 hex_to_float(const char* s);

			/**
			 * Returns a double-precision floating point number from its bit representation in hexadecimal (ieee754).
			 * 
			 * @param s pointer to base of string of the bit representation in hexadecimal (ieee754).
			 */
			f64 hex_to_double(const char* s);

			/**
			 * Returns a parsed 64-bit unsigned integer.
			 * 
			 * @param s pointer to start of string of integer.
			 * @param end pointer to end of string of integer.
			 * @param out pointer to integer.
			 * @return number of characters parsed. 0 means there was an error (either none parsed or number doesn't fit in a u64).
			 */
			size_type to_u64(const char* s, const char* end, u64* out);

			/**
			 * Returns a parsed 64-bit signed integer.
			 * 
			 * @param s pointer to start of string of integer.
			 * @param end pointer to end of string of integer.
			 * @param out pointer to integer.
			 * @return number of characters parsed. 0 means there was an error (either none parsed or number doesn't fit in a s64).
			 */
			size_type to_s64(const char* s, const char* end, s64* out);
		}
	}	
}

#endif // JSON_INCLUDE_H

#ifdef JSON_IMPLEMENTATION

#ifndef JSON_NDEBUG

#define JSON_HERE() printf("Here: %s, %d, %s\n", __FILE__, __LINE__, __func__)

#define JSON_ASSERT(EXPR) \
do { \
	if(!(EXPR)) { \
		fprintf(stdout, "%s(%d): Assertion \"%s\" failed. errno: %s.\n", __FILE__, __LINE__, #EXPR, (errno == 0 ? "None" : strerror(errno))); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

#define JSON_ASSERTF(EXPR, FMT, ...) \
do { \
	if(!(EXPR)) { \
		fprintf(stdout, "%s(%d): Assertion \"%s\" failed. errno: %s.", __FILE__, __LINE__, #EXPR, (errno == 0 ? "None" : strerror(errno))); \
		fprintf(stdout, FMT"\n", __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

#else

#define JSON_HERE()
#define JSON_ASSERTF(EXPR, FMT, ...)
#define JSON_ASSERT(EXPR)

#endif

#ifndef JSON_STRTOD // define this macro if you have your own strtod implementation.

#include <cstdlib> // strtod

// begin, end for the valid range that needs to be parsed.
#define JSON_STRTOD(dest, begin, end) \
do { \
	char* endptr; \
	*(dest) = strtod((begin), &endptr); \
	JSON_ASSERT(((end) == endptr) && "Failed to parse float."); \
} while (0)

#endif

#ifdef JSON_NO_FLOAT // define this macro if you don't intend to read (non-hex bit representation) floats.
#undef JSON_STRTOD
#define JSON_STRTOD(dest, begin, end)
#endif

namespace ers
{
	namespace json
	{
		JsonNode::JsonNode(const JsonToken& token)
		{
			as_sv.data = token.data;
			as_sv.length = token.length;
			next = nullptr;		
			switch (token.type)
			{
				case JsonTokenType::JSON_ARRAY_BEGIN:
				{
					type = JsonNodeType::JSON_ARRAY;
					count = 0;
				} break;
				case JsonTokenType::JSON_OBJECT_BEGIN:
				{
					type = JsonNodeType::JSON_OBJECT;
					count = 0;
				} break;
				case JsonTokenType::JSON_STRING:
				{
					type = JsonNodeType::JSON_STRING;
				} break;
				case JsonTokenType::JSON_KEY:
				{
					type = JsonNodeType::JSON_KEY;
				} break;	
				case JsonTokenType::JSON_TRUE:
				{
					type = JsonNodeType::JSON_TRUE;
				} break;	
				case JsonTokenType::JSON_FALSE:
				{
					type = JsonNodeType::JSON_FALSE;
				} break;
				case JsonTokenType::JSON_NULL:
				{
					type = JsonNodeType::JSON_NULL;
				} break;		
				case JsonTokenType::JSON_NUMBER:
				{
					type = JsonNodeType::JSON_NUMBER;
				} break;
				case JsonTokenType::JSON_FLOAT_HEX:
				{
					type = JsonNodeType::JSON_FLOAT_HEX;
				} break;
				case JsonTokenType::JSON_DOUBLE_HEX:
				{
					type = JsonNodeType::JSON_DOUBLE_HEX;
				} break;
				case JsonTokenType::INVALID:
				{
					type = JsonNodeType::INVALID;
				} break;
				case JsonTokenType::SYNTACTIC_ERROR:
				{
					type = JsonNodeType::SYNTACTIC_ERROR;
				} break;	
				case JsonTokenType::JSON_EOF:
				{
					type = JsonNodeType::JSON_EOF;
				} break;			
				default:
				{
					JSON_ASSERT(false && "Unreachable.");
				} break;
			}
		}

		const JsonNode* JsonNode::GetFirst() const
		{
			return (IsComplex() && count != 0) ? (this + 1) : nullptr;
		}	

		const JsonNode* JsonNode::GetValue() const
		{
			return IsKey() ? (this + 1) : (IsValue() ? this : nullptr);
		}	

		const JsonNode* JsonNode::GetNext() const
		{
			return next;
		}	

		int JsonNode::GetAsBool(bool* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			int result = 1;
			if (type == JsonNodeType::JSON_TRUE)
			{
				*dest = true;
			}
			else if (type == JsonNodeType::JSON_FALSE)
			{
				*dest = false;
			}
			else
			{
				result = 0;	
			}

			return result;
		}	

		int JsonNode::GetAsFloat(f32* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			int result = 1;
			if (type == JsonNodeType::JSON_FLOAT_HEX)
			{
				*dest = util::hex_to_float(as_sv.data);
			}
			else if (type == JsonNodeType::JSON_NUMBER || type == JsonNodeType::JSON_DOUBLE_HEX)
			{
				f64 temp = 0.0;
				result = GetAsDouble(&temp);
				*dest = static_cast<f32>(temp);
			}	
			else
			{
				result = 0;	
			}

			return result;
		}	

		int JsonNode::GetAsDouble(f64* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			int result = 1;
			if (type == JsonNodeType::JSON_NUMBER)
			{
				JSON_STRTOD(dest, as_sv.data, as_sv.data + as_sv.length);
			}
			else if (type == JsonNodeType::JSON_DOUBLE_HEX)
			{
				*dest = util::hex_to_double(as_sv.data);
			}
			else if (type == JsonNodeType::JSON_FLOAT_HEX)
			{
				*dest = static_cast<f64>(util::hex_to_float(as_sv.data));
			}	
			else
			{
				result = 0;	
			}

			return result;
		}	

		int JsonNode::GetAsU64(u64* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			int result;
			if (type == JsonNodeType::JSON_NUMBER)
			{
				result = (int)util::to_u64(as_sv.data, as_sv.data + as_sv.length, dest);
			}
			else 
			{
				result = 0;
			}

			return result > 0 ? 1 : 0;
		}	

		int JsonNode::GetAsS64(s64* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			int result;
			if (type == JsonNodeType::JSON_NUMBER)
			{
				result = (int)util::to_s64(as_sv.data, as_sv.data + as_sv.length, dest);
			}
			else 
			{
				result = 0;
			}

			return result > 0 ? 1 : 0;
		}	

		size_type JsonNode::GetAsString(char* dest) const
		{
			JSON_ASSERT(dest != nullptr);	

			size_type result = 0;
			if (type == JsonNodeType::JSON_STRING || type == JsonNodeType::JSON_KEY)
			{
				result = util::json_string_to_utf8(dest, as_sv.data + 1, as_sv.length - 2);  // skips quotation marks
			}

			return result;
		}	

		string_view JsonNode::GetAsStringView() const
		{
			string_view result;
			if (type != JsonNodeType::JSON_ARRAY && type != JsonNodeType::JSON_OBJECT)
			{
				result = as_sv;
			}
			else
			{
				JSON_ASSERTF(false, "%s", "Value cannot be read as a string view.");
			}

			return result;
		}	

		size_type JsonNode::GetCount() const 
		{
			JSON_ASSERTF(IsComplex(), "%s", "Value is not an object or and array.");
			return count;
		}

		bool JsonNode::IsKey() const
		{
			return (type == JsonNodeType::JSON_KEY);
		}

		bool JsonNode::IsValue() const
		{
			return !IsKey() && !IsInvalid();
		}

		bool JsonNode::IsComplex() const
		{
			return (IsArray() || IsObject());
		}

		bool JsonNode::IsInvalid() const
		{
			return (type == JsonNodeType::INVALID || type == JsonNodeType::SYNTACTIC_ERROR);
		}

		bool JsonNode::IsNumber() const
		{
			return (type == JsonNodeType::JSON_NUMBER 
				|| type == JsonNodeType::JSON_FLOAT_HEX || type == JsonNodeType::JSON_DOUBLE_HEX);
		}

		bool JsonNode::IsBool() const
		{
			return (type == JsonNodeType::JSON_TRUE || type == JsonNodeType::JSON_FALSE);
		}

		bool JsonNode::IsString() const
		{
			return (type == JsonNodeType::JSON_STRING || type == JsonNodeType::JSON_KEY);
		}

		bool JsonNode::IsNull() const
		{
			return (type == JsonNodeType::JSON_NULL);
		}

		bool JsonNode::IsArray() const
		{
			return (type == JsonNodeType::JSON_ARRAY);
		}

		bool JsonNode::IsObject() const
		{
			return (type == JsonNodeType::JSON_OBJECT);
		}

		bool JsonNode::IsEOF() const
		{
			return (type == JsonNodeType::JSON_EOF);
		}

		const JsonNode* get_value_node(const JsonNode* node_begin, const char* path, size_type path_string_length)
		{
			const char* p = path;
			const char* end = p + path_string_length;
			s64 len = (s64)path_string_length; // length necessary for matching keys.	

			JSON_ASSERT(node_begin->IsArray() || node_begin->IsObject());
			const JsonNode* current_node = node_begin;	

			while (p < end && !current_node->IsEOF() && current_node != nullptr)
			{
				if (*p == '['
					&& (current_node->IsArray()))
				{
					const size_type count = current_node->GetCount();

					++p; --len; ++current_node;	
					if (count == 0) // Array is empty.
					{
						current_node = nullptr;
						break;
					}	

					// Get array index.
					bool sign = false;
					if (p != end && *p == '-')
					{
						sign = true;
						++p; --len; 
					}

					size_type idx;
					size_type idx_len = util::to_u64(p, end, &idx);
					if (idx_len == 0)
					{
						current_node = nullptr;
						break;
					}	
					p += idx_len; len -= idx_len;

					idx = (idx >= count) ? (idx % count) : idx;					
					idx = (sign && idx != 0) ? (count - idx) : idx;

					if (p != end && *p == ']')
					{
						++p; --len;
					}
					else
					{
						current_node = nullptr;
						break;
					}	

					// Skip to indexed item.
					for (size_type j = 0; j < idx; j++)
					{
						current_node = current_node->GetNext();
						if (current_node == nullptr)
						{
							break;
						}
					}
				}
				else if (*p == '.' && current_node->IsObject())
				{
					const size_type count = current_node->GetCount();

					++p; --len; ++current_node;
					if (count == 0) // Object is empty.
					{
						current_node = nullptr;
						break;
					}	

					while (p < end && !current_node->IsEOF() && current_node->IsKey())
					{
						bool matched = false;
						const char* node_data;
						s64 node_length;
						node_data = current_node->as_sv.data;
						node_length = (s64)current_node->as_sv.length;
						if (*p == '\"' && len >= node_length && memcmp(p, node_data, node_length) == 0)
						{
							p += node_length;
							len -= node_length;
							matched = true;
						}
						else if (*p != '\"' && len >= (node_length - 2) && memcmp(p, node_data + 1, node_length - 2) == 0)
						{
							p += node_length - 2;
							len -= node_length - 2;
							matched = true;
						}	

						if (matched)
						{
							current_node += 1;
							break;
						}	

						current_node = current_node->GetNext();
						if (current_node == nullptr)
						{
							break;
						}
					}
				}
				else
				{
					current_node = nullptr;
					break;
				}
			}	

			return current_node;
		}	

		template <size_type NODE_CAPACITY>
		FlatJson<NODE_CAPACITY>::FlatJson()
		{
			m_count = 0;
			memset(&m_nodes[0], 0, NODE_CAPACITY * sizeof(JsonNode));
		}

		template <size_type NODE_CAPACITY>
		JsonNode& FlatJson<NODE_CAPACITY>::operator[](size_type i)
		{
			JSON_ASSERTF(i < NODE_CAPACITY, "%s", "FlatJson::operator[]: Out of bounds.");
			return m_nodes[i];
		}

		template <size_type NODE_CAPACITY>
		const JsonNode& FlatJson<NODE_CAPACITY>::operator[](size_type i) const
		{
			JSON_ASSERTF(i < NODE_CAPACITY, "%s", "FlatJson::operator[]: Out of bounds.");
			return m_nodes[i];
		}

		template <size_type NODE_CAPACITY>
		JsonNode& FlatJson<NODE_CAPACITY>::GetBegin()
		{
			return this->operator[](0);
		}

		template <size_type NODE_CAPACITY>
		const JsonNode& FlatJson<NODE_CAPACITY>::GetBegin() const
		{
			return this->operator[](0);
		}

		template <size_type NODE_CAPACITY>
		const JsonNode* FlatJson<NODE_CAPACITY>::GetValueNode(const char* path, const JsonNode* node) const
		{
			return get_value_node((node == nullptr) ? &m_nodes[0] : node, path, strlen(path));
		}	

		template <size_type NODE_CAPACITY>
		int FlatJson<NODE_CAPACITY>::GetAsBool(const char* path, bool* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsBool(dest);
		}	

		template <size_type NODE_CAPACITY>
		int FlatJson<NODE_CAPACITY>::GetAsFloat(const char* path, f32* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsFloat(dest);
		}	

		template <size_type NODE_CAPACITY>
		int FlatJson<NODE_CAPACITY>::GetAsDouble(const char* path, f64* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsDouble(dest);
		}	

		template <size_type NODE_CAPACITY>
		int FlatJson<NODE_CAPACITY>::GetAsU64(const char* path, u64* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsU64(dest);
		}	

		template <size_type NODE_CAPACITY>
		int FlatJson<NODE_CAPACITY>::GetAsS64(const char* path, s64* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsS64(dest);
		}	

		template <size_type NODE_CAPACITY>
		size_type FlatJson<NODE_CAPACITY>::GetAsString(const char* path, char* dest, const JsonNode* n) const
		{
			const JsonNode* node = GetValueNode(path, n);
			return (node == nullptr) ? 0 : node->GetAsString(dest);
		}	

		template <size_type NODE_CAPACITY>
		size_type FlatJson<NODE_CAPACITY>::GetCount() const
		{
			return m_count;
		}

		template <size_type NODE_CAPACITY>
		size_type FlatJson<NODE_CAPACITY>::GetCapacity() const
		{
			return NODE_CAPACITY;
		}

		JsonParser::JsonParser(const char* data_, size_type length_, JsonNode* buffer, size_type capacity)
		{
			SetUp(data_, length_, buffer, capacity);
		}	

		void JsonParser::SetUp(const char* data_, size_type length_, JsonNode* buffer, size_type capacity)
		{
			m_begin = data_;	
			m_end = m_begin + length_;

			m_pos = data_;
			m_errorCode = JsonErrorCode::NOT_DONE;

			m_nodeBuffer = buffer;
			m_nodeCount = 0;
			m_tokenCapacity = capacity;

			m_currentLine = 1;

			memset(&m_errorLog[0], 0, JSON_ERROR_MESSAGE_LENGTH + 1);
			m_errorLogPos = &m_errorLog[0];
		}	

		template <size_type NODE_CAPACITY>
		void JsonParser::Parse(FlatJson<NODE_CAPACITY>& flat_json)
		{
			Parse(&flat_json[0], NODE_CAPACITY);
			flat_json.m_count = m_nodeCount;
		}

		void JsonParser::Parse(JsonNode* new_buffer, size_type new_capacity) 
		{
			if (new_buffer != nullptr)
			{
				m_pos = m_begin;
				m_errorCode = JsonErrorCode::NOT_DONE;

				m_nodeBuffer = new_buffer;
				m_nodeCount = 0;
				m_tokenCapacity = new_capacity;

				m_currentLine = 1;
				memset(&m_errorLog[0], 0, JSON_ERROR_MESSAGE_LENGTH + 1);
				m_errorLogPos = &m_errorLog[0];
			} 

			const bool is_valid = IsValid();
			if (is_valid) // if valid, do not re-tokenize.
			{
				return;
			}	

			skipWhitespace();

			bool loop_result = true; // keeps track of whether there's been errors during tokenization.	
			while (loop_result && m_pos < m_end)
			{
				m_currentToken = getNextToken();
				const bool isArray = (m_currentToken.type == JsonTokenType::JSON_ARRAY_BEGIN);
				const bool isObject = (m_currentToken.type == JsonTokenType::JSON_OBJECT_BEGIN);
				loop_result = expect(isArray || isObject, "Array or object expected");
				if (!loop_result) break;		
				pushNode();	

				if (isArray) 
				{
					loop_result = parseArray();
				}
				else if (isObject) 
				{
					loop_result = parseObject();
				}	
			}	

            if (loop_result)
            {
				m_currentToken = getNextToken();
                loop_result = pushNode();
            }

			if (loop_result)
            {
				m_errorCode = JsonErrorCode::VALID_JSON;
            }
			else
			{
				logInvalidTokenPosition();
			}
		}

		bool JsonParser::parseArray() 
		{
			bool result = true;

			m_currentToken = getNextToken();
			if (m_currentToken.type == JsonTokenType::JSON_ARRAY_END) return result;
			
			JsonNode* complex_node = getLastNode();
			JsonNode* previous_value_ptr = nullptr;
			while (result && m_pos < m_end) 
			{			
				const bool is_first_node = (previous_value_ptr == nullptr);
				const bool is_primitive = isPrimitiveValueToken(&m_currentToken);
				const bool is_array = (m_currentToken.type == JsonTokenType::JSON_ARRAY_BEGIN);
				const bool is_object = (m_currentToken.type == JsonTokenType::JSON_OBJECT_BEGIN);
				result = expect(is_primitive || is_array || is_object, is_first_node ? "value or array end expected" : "value expected");	
				if (!result) break;

				result = pushNode();				
                if (!result) break;
				++complex_node->count;

				JsonNode* last_node_ptr = getLastNode();
				if (!is_first_node) previous_value_ptr->next = last_node_ptr;
				previous_value_ptr = last_node_ptr;

				if (is_array) result = parseArray(); 
				else if (is_object) result = parseObject();
				if (!result) break;

				m_currentToken = getNextToken();
				if (m_currentToken.type == JsonTokenType::JSON_ARRAY_END) break;

				result = expect(m_currentToken.type == JsonTokenType::JSON_COMMA, "comma or array end expected");	
				if (!result) break;

				m_currentToken = getNextToken();
			}

			return result;
		}

		bool JsonParser::parseObject() 
		{
			bool result = true;

			m_currentToken = getNextToken();
			if (m_currentToken.type == JsonTokenType::JSON_OBJECT_END) return result;

			JsonNode* complex_node = getLastNode();
			JsonNode* previous_key_ptr = nullptr;
			JsonNode* previous_value_ptr = nullptr;
			while (result && m_pos < m_end) 
			{
				const bool is_first_node = (previous_value_ptr == nullptr);
				result = expect(m_currentToken.type == JsonTokenType::JSON_STRING, is_first_node ? "string (key) or object end expected" : "string (key) expected");		
				if (!result) break;
				m_currentToken.type = JsonTokenType::JSON_KEY;

				result = pushNode();
				if (!result) break;
                
				JsonNode* last_node_ptr = getLastNode();
				if (previous_key_ptr != nullptr) previous_key_ptr->next = last_node_ptr;
				previous_key_ptr = last_node_ptr;

				m_currentToken = getNextToken();
				result = expect(m_currentToken.type == JsonTokenType::JSON_COLON, "colon expected"); if(!result) break;				

				m_currentToken = getNextToken();
				const bool is_primitive = isPrimitiveValueToken(&m_currentToken);
				const bool is_array = (m_currentToken.type == JsonTokenType::JSON_ARRAY_BEGIN);
				const bool is_object = (m_currentToken.type == JsonTokenType::JSON_OBJECT_BEGIN);
				result = expect(is_primitive || is_array || is_object, "value expected");
				if (!result) break;

				result = pushNode();		
                if (!result) break;
				++complex_node->count;

				last_node_ptr = getLastNode();
				if (previous_value_ptr != nullptr) previous_value_ptr->next = last_node_ptr;
				previous_value_ptr = last_node_ptr;

				if (is_array) result = parseArray();
				else if (is_object) result = parseObject();
				if (!result) break;

				m_currentToken = getNextToken();
				if (m_currentToken.type == JsonTokenType::JSON_OBJECT_END) break;

				result = expect(m_currentToken.type == JsonTokenType::JSON_COMMA, "comma or object end expected");	
				if (!result) break;

				m_currentToken = getNextToken();
			}

			return result;
		}

		inline bool JsonParser::expect(bool expected, const char* message) 
		{
			if (!expected)
			{
				if (m_currentToken.type != JsonTokenType::INVALID) 
				{
					m_currentToken.type = JsonTokenType::SYNTACTIC_ERROR;
                    m_errorCode = JsonErrorCode::SYNTACTIC_ERRORS;
					appendToErrorLog("Syntactic error at line %u: %s\n", (u32)m_currentLine, message);
				}
                else
                {
                    m_errorCode = JsonErrorCode::INVALID_TOKENS;
                }		
				pushNode();
			}
			return expected;
		} 

		bool JsonParser::pushNode() 
		{
            bool result = true;
            if (m_nodeCount < m_tokenCapacity) 
            {
                m_nodeBuffer[m_nodeCount++] = JsonNode(m_currentToken);
            }
            else
            {
                m_errorCode = JsonErrorCode::CAPACITY_EXCEEDED;
				appendToErrorLog("Exceeded node buffer capacity\n");
                result = false;
            }
			return result;			
		}

		void JsonParser::logInvalidTokenError(JsonTokenType actual_type, const char* message)
		{
			if (actual_type == JsonTokenType::INVALID)
			{
				appendToErrorLog("Invalid token at line %u: %s\n", (u32)m_currentLine, message);
			}
		}

		void JsonParser::appendToErrorLog(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);

			size_type error_message_length = (m_errorLogPos - &m_errorLog[0]);
			size_type error_message_rest = JSON_ERROR_MESSAGE_LENGTH + 1 - error_message_length;
			int written_count = std::vsnprintf(m_errorLogPos, error_message_rest, fmt, args);		
			JSON_ASSERTF(written_count >= 0, "%s", "JsonParser::appendToErrorLog: encoding error");
			JSON_ASSERTF(error_message_length + written_count <= JSON_ERROR_MESSAGE_LENGTH, "%s", "JsonParser::appendToErrorLog: error message size exceeded");
			m_errorLogPos += written_count;
			
			va_end(args);
		}

		constexpr size_type JSON_INVALID_TOKEN_NEWLINES = 3;
		void JsonParser::logInvalidTokenPosition() 
		{
			const char* begin = m_currentToken.data;
			const char* end = m_currentToken.data + m_currentToken.length;

			size_type newline_count = 0;
			const char* before_begin = begin;
			while (newline_count < JSON_INVALID_TOKEN_NEWLINES && before_begin != m_begin) 
			{
				newline_count += (*before_begin-- == '\n') ? 1 : 0;
			}
			++before_begin;

			newline_count = 0;
			const char* after_end = end;
			while (newline_count < JSON_INVALID_TOKEN_NEWLINES && after_end != m_end) 
			{
				newline_count += (*after_end++ == '\n') ? 1 : 0;
			}
			--after_end;

			appendToErrorLog(
				"...\n%.*s >>> %.*s <<< %.*s\n...\n", 
				(u32)(begin - before_begin), before_begin, 
				(u32)m_currentToken.length, m_currentToken.data, 
				(u32)(after_end - end), end
			);
		}

		JsonNode* JsonParser::getLastNode() 
		{
			return &m_nodeBuffer[m_nodeCount - 1];
		}

		void JsonParser::skipWhitespace() 
		{
			while (m_pos != m_end && isWhitespace(*m_pos))
			{
				m_currentLine += (*m_pos++ == '\n') ? 1 : 0;
			}	
		}

		JsonToken JsonParser::getNextToken() 
		{
			JsonToken result;

			const char* start = m_pos;			
			result.data = start;

            if (m_pos >= m_end) {
                result.length = 0;
                result.type = JsonTokenType::JSON_EOF;
                return result;
            }

			char ch = *m_pos;
			if (isDigit(ch) || ch == '-')
			{
				const char* q = m_pos + 1;
				if (q < m_end && *q == 'x')
				{
					m_pos += 2;
					int rc = matchFloatHex();
					switch (rc)
					{
					case 0:
						result.type = JsonTokenType::INVALID;
						break;
					case 1:
						result.type = JsonTokenType::JSON_FLOAT_HEX;
						break;
					case 2:
						result.type = JsonTokenType::JSON_DOUBLE_HEX;
						break;
					default:
						JSON_ASSERT(false && "Unreachable.");
					}
					logInvalidTokenError(result.type, "floating point number in hexadecimal expected");
				}
				else
				{
					result.type = (matchNumber()) ?
						JsonTokenType::JSON_NUMBER : JsonTokenType::INVALID;
					logInvalidTokenError(result.type, "number expected");
				}
			}
			else if (ch == '\"')
			{
				result.type = (matchString()) ?
					JsonTokenType::JSON_STRING : JsonTokenType::INVALID;
				logInvalidTokenError(result.type, "string expected");
			}
			else if (ch == 't')
			{
				result.type = (matchLiteral("rue", 3)) ?
					JsonTokenType::JSON_TRUE : JsonTokenType::INVALID;
				logInvalidTokenError(result.type, "true expected");
			}
			else if (ch == 'f')
			{
				result.type = (matchLiteral("alse", 4)) ?
					JsonTokenType::JSON_FALSE : JsonTokenType::INVALID;
				logInvalidTokenError(result.type, "false expected");
			}
			else if (ch == 'n')
			{
				result.type = (matchLiteral("ull", 3)) ?
					JsonTokenType::JSON_NULL : JsonTokenType::INVALID;
				logInvalidTokenError(result.type, "null expected");
			}
			else if (ch == '{')
			{
				result.type = JsonTokenType::JSON_OBJECT_BEGIN;
				++m_pos;
			}
			else if (ch == '[')
			{
				result.type = JsonTokenType::JSON_ARRAY_BEGIN;
				++m_pos;
			}
			else if (ch == '}')
			{
				result.type = JsonTokenType::JSON_OBJECT_END;
				++m_pos;
			}
			else if (ch == ']')
			{
				result.type = JsonTokenType::JSON_ARRAY_END;
				++m_pos;
			}
			else if (ch == ':')
			{
				result.type = JsonTokenType::JSON_COLON;
				++m_pos;
			}
			else if (ch == ',')
			{
				result.type = JsonTokenType::JSON_COMMA;
				++m_pos;
			}
			else // Invalid case.
			{			
				result.type = JsonTokenType::INVALID;				
				appendToErrorLog("Invalid token at line %u\n", (u32)m_currentLine);
			}	

			if (result.type == JsonTokenType::INVALID) 
			{
				++m_pos;
				while (m_pos != m_end && !isValid(*m_pos))
				{
					++m_pos;
				}
			}

			result.length = m_pos - start;
			
			skipWhitespace();

			return result;
		}

		bool JsonParser::IsValid() const
		{
			return (m_errorCode == JsonErrorCode::VALID_JSON);
		}	

		size_type JsonParser::GetCount() const
		{
			return m_nodeCount;
		}	

		size_type JsonParser::GetCapacity() const
		{
			return m_tokenCapacity;
		}	

		JsonErrorCode JsonParser::GetErrorCode() const
		{
			return m_errorCode;
		}

		const char* JsonParser::GetErrorMessage() const
		{
			return m_errorLog;
		}

		bool JsonParser::matchString()
		{
			// Parse string.
			++m_pos;	

			while (m_pos < m_end && *m_pos != '\"')
			{
				u8 ch = *m_pos;
				if (ch < 0x20 || ch == '/' || ch == '\"')
				{
					break;
				}
				else if (ch == '\\')
				{
					++m_pos;
					ch = *m_pos;
					if (ch == '\\' || ch == '/' || ch == '\"' || ch == '0' 
						|| ch == 'a' || ch == 'b' || ch == 't' || ch == 'v' 
						|| ch == 'f' || ch == 'r' || ch == 'n')
					{
						++m_pos;
					}
					else if (ch == 'u')
					{
						++m_pos;
						for (u8 count = 0; count < 4; count++)
						{
							if (m_pos == m_end || !isHexDigit(*m_pos))
							{
								break;
							}
							++m_pos;
						}
					}
					else
					{
						break;
					}
				}
				else if (ch < 0xFF)
				{
					++m_pos;
				}
				else // assume utf-8 encoding
				{
					size_type len = util::utf8_len(*m_pos);
					if (len == 0)
					{
						break;
					}
					else
					{
						m_pos += len;
					}
				}
			}	

			const bool result = (m_pos != m_end && *m_pos == '\"');	

			// Only advance if the whole string has been accepted.
			// If not, we are already at the start of the next token.
			if (result)
			{
				++m_pos;
			}	

			return result;
		}	

		bool JsonParser::matchNumber()
		{
			bool result = true;	

			if (m_pos != m_end && *m_pos == '-')
			{
				++m_pos;
			}	

			char ch = *m_pos;
			if (ch == '0')
			{
				++m_pos;
			}
			else if (ch >= '1' && ch <= '9')
			{
				++m_pos;
				while (m_pos != m_end && isDigit(*m_pos))
				{
					++m_pos;
				}
			}
			else
			{
				result = false;
			}	

			auto do_digits = [&]() -> void
			{
				if (m_pos != m_end && isDigit(*m_pos))
				{
					++m_pos;
					while (m_pos != m_end && isDigit(*m_pos))
					{
						++m_pos;
					}
				}
				else
				{
					result = false;
				}
			};	

			bool fraction_done = false;
			while (result && !fraction_done && m_pos < m_end)
			{
				switch (*m_pos)
				{
				case '.':
				{
					++m_pos;
					do_digits();
				} break;
				case 'e':
				case 'E':
				{
					++m_pos;
					if (m_pos != m_end && (*m_pos == '-' || *m_pos == '+'))
					{
						++m_pos;
					}
					do_digits();
					fraction_done = true;
				} break;
				default:
					fraction_done = true;
				}
			}	

			return result;
		}	

		bool JsonParser::matchLiteral(const char* s, u8 len)
		{
			bool result = true;	

			for (u8 i = 0; i < len; i++)
			{
				++m_pos;
				if (m_pos == m_end || *m_pos != s[i])
				{
					result = false;
					break;
				}
			}	

			if (result)
			{
				++m_pos;
			}	

			return result;
		}	

		int JsonParser::matchFloatHex()
		{
			int result = 0;		

			u8 count = 0;
			while (m_pos != m_end && isHexDigit(*m_pos))
			{
				++count;
				++m_pos;
			}	

			if (count == 8)
			{
				result = 1; // float
			}
			else if (count == 16)
			{
				result = 2; // double	
			}

			return result;
		}	

		bool JsonParser::isDigit(char ch) const
		{
			return (ch >= '0' && ch <= '9');
		}	

		bool JsonParser::isHexDigit(char ch) const
		{
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
		}	

		bool JsonParser::isWhitespace(char ch) const
		{
			return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
		}	

		bool JsonParser::isStructural(char ch) const
		{
			return (ch == '{' || ch == '[' || ch == '}' || ch == ']' || ch == ':' || ch == ',');
		}	

		bool JsonParser::isValid(char ch) const
		{
			return isWhitespace(ch) || isStructural(ch) || isDigit(ch) || ch == 't' || ch == 'f' || ch == 'n';
		}	

		bool JsonParser::isPrimitiveValueToken(JsonToken* t) const
		{
			return t->type == JsonTokenType::JSON_TRUE
				|| t->type == JsonTokenType::JSON_FALSE
				|| t->type == JsonTokenType::JSON_NULL
				|| t->type == JsonTokenType::JSON_NUMBER
				|| t->type == JsonTokenType::JSON_STRING
				|| t->type == JsonTokenType::JSON_FLOAT_HEX
				|| t->type == JsonTokenType::JSON_DOUBLE_HEX;
		}	

		namespace util 
		{
			void print_nodes(const JsonNode* node_buffer, size_type node_count)
			{
#ifndef JSON_NDEBUG
				for (size_type i = 0; i < node_count; i++)
				{
					print_node(&node_buffer[i]);
				}
#endif
			}	

			void print_node(const JsonNode* n)
			{
#ifndef JSON_NDEBUG
				switch (n->type)
				{
				case JsonNodeType::JSON_ARRAY:
					printf("ARRAY: count = %u\n", (u32)n->count);
					break;
				case JsonNodeType::JSON_OBJECT:
					printf("OBJECT: count = %u\n", (u32)n->count);
					break;
				case JsonNodeType::JSON_TRUE:
					printf("TRUE: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_FALSE:
					printf("FALSE: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_NULL:
					printf("NULL: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_NUMBER:
					printf("NUMBER: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_STRING:
					printf("STRING: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_KEY:
					printf("KEY: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_FLOAT_HEX:
					printf("FLOAT (HEX): %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_DOUBLE_HEX:
					printf("DOUBLE (HEX): %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::JSON_EOF:
					printf("EOF: done!\n");
					break;
				case JsonNodeType::SYNTACTIC_ERROR:
					printf("[ERROR]:\n    ");
					printf("SYNTACTIC ERROR: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				case JsonNodeType::INVALID:
					printf("[ERROR]:\n    ");
					printf("INVALID TOKEN: %.*s\n", (u32)n->as_sv.length, n->as_sv.data);
					break;
				default:
					(void)0;
				}
#else
				(void)t;
#endif
			}	
			size_type utf8_len(char ch)
			{
				const u8 c = (ch >> 3);
				static const size_type length_from_msb[32] =
				{
					1, 1, 1, 1, 1, 1, 1, 1,	1, 1, 1, 1,	1, 1, 1, 1,	 // 0xxx xxxx
					0, 0, 0, 0,	0, 0, 0, 0, // 10xx xxxx, invalid
					2, 2, 2, 2, // 110x xxxx
					3, 3, // 1110 xxxx
					4, // 1111 0xxx
					0 // 1111 1xxx, invalid
				};
				return length_from_msb[c];
			}	

			u32 json_string_character_to_codepoint(const char* s, size_type* idx)
			{
				const u8* p = (const u8*)s + *idx;
				size_type len = 0; // num of bytes processed	

				u32 result = 0;
				u8 ch = *p;
				if (ch == '\\')
				{
					++p; len += 1;
					ch = *p;
					if (ch == '\\' || ch == '/' || ch == '\"')
					{
						result = (u32)ch;
						len += 1;
					}
					else if (ch == 'u')
					{
						++p; len += 1;
						result = 0;
						for (u8 count = 0; count < 4; count++)
							result = result * 16 + hex_digit_to_u32(*p++);
						len += 4;	

						if (result >= 0xd800 && result <= 0xdbff)
						{
							p += 2;
							u32 extra = 0;
							for (u8 count = 0; count < 4; count++)
								extra = extra * 16 + hex_digit_to_u32(*p++);
							result = ((result - 0xd800) << 10) + ((u32)extra - 0xdc00) + 0x10000;
							len += 6;
						}
					}
					else
					{
						len += 1;
						switch (ch)
						{
						case '0':
							result = (u32)'\0';
							break;
						case 'a':
							result = (u32)'\a';
							break;
						case 'b':
							result = (u32)'\b';
							break;
						case 't':
							result = (u32)'\t';
							break;
						case 'v':
							result = (u32)'\v';
							break;
						case 'f':
							result = (u32)'\f';
							break;
						case 'r':
							result = (u32)'\r';
							break;
						case 'n':
							result = (u32)'\n';
							break;
						default:
							JSON_ASSERTF(false, "%s", "JsonParser::json_string_character_to_codepoint.");
						}				
					}
				}
				else // assume utf-8 encoding.
				{
					len = utf8_len(ch);
					JSON_ASSERT(len > 0);
					static const u8 masks[5] = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 }; // masks[0] is a dummy 
					static const u8 shift_values[7] = { 0, 0, 0, 0, 6, 12, 18 };	

					u8 buffer[4];
					buffer[0] = ch;	
					buffer[1] = (len > 1) ? p[1] : 0;
					buffer[2] = (len > 2) ? p[2] : 0;
					buffer[3] = (len > 3) ? p[3] : 0;	

					result = (buffer[0] & masks[len]) << shift_values[len + 2];
					result |= (buffer[1] & 0x3f) << shift_values[len + 1];
					result |= (buffer[2] & 0x3f) << shift_values[len];
					result |= (buffer[3] & 0x3f);
				}	

				*idx += len;
				return result;
			}	

			size_type json_string_to_utf8(char* dest, const char* src, size_type length)
			{
				const u8* p = (const u8*)src;
				const u8* end = p + length;
				u8* out = (u8*)dest;
				size_type len = 0;	

				while (p < end)
				{
					u8 ch = *p;
					if (ch == '\\')
					{	

						++p;
						ch = *p;
						if (ch == '\\' || ch == '/' || ch == '\"')
						{
							if (out)
							{						
								*out++ = *p++;
							}
							else
							{
								++p;
								++len;
							}
						}
						else if (ch == 'u') // calculate codepoint
						{
							++p;
							u32 codepoint = 0;
							for (u8 count = 0; count < 4; count++)
							{
								JSON_ASSERT(p < end);
								codepoint = codepoint * 16 + hex_digit_to_u32(*p++);
							}	

							if (codepoint >= 0xd800 && codepoint <= 0xdbff) // supplementary planes
							{
								p += 2; // skip \u
								u32 extra = 0;
								for (u8 count = 0; count < 4; count++)
								{
									JSON_ASSERT(p < end);
									extra = extra * 16 + hex_digit_to_u32(*p++);
								}
								codepoint = ((codepoint - 0xd800) << 10) + ((u32)codepoint - 0xdc00) + 0x10000;
							}	

							JSON_ASSERTF(codepoint < 0x110000,
								"%s", "JsonParser::json_string_to_utf8: Invalid codepoint.");	

							// Decode to UTF-8
							if (out)
							{
								if (codepoint < 0x80)
								{
									*out++ = (u8)codepoint;
								}
								else if (codepoint < 0x800)
								{
									*out++ = (u8)(((codepoint >> 6) & 0x1f) | 0xc0);
									*out++ = (u8)((codepoint & 0x3f) | 0x80);
								}
								else if (codepoint < 0x10000)
								{
									*out++ = (u8)(((codepoint >> 12) & 0x0f) | 0xe0);
									*out++ = (u8)(((codepoint >> 6) & 0x3f) | 0x80);
									*out++ = (u8)((codepoint & 0x3f) | 0x80);
								}
								else if (codepoint < 0x110000)
								{
									*out++ = (u8)(((codepoint >> 18) & 0x07) | 0xf0);
									*out++ = (u8)(((codepoint >> 12) & 0x3f) | 0x80);
									*out++ = (u8)(((codepoint >> 6) & 0x3f) | 0x80);
									*out++ = (u8)((codepoint & 0x3f) | 0x80);
								}
							}
							else
							{
								len += (1 + (size_type)((codepoint) >= 0x80)
									+ (size_type)((codepoint) >= 0x800) + (size_type)((codepoint) >= 0x10000));
							}
						}
						else
						{
							if (out)
							{
								switch (ch)
								{
								case '0':
									*out++ = '\0';
									break;
								case 'a':
									*out++ = '\a';
									break;
								case 'b':
									*out++ = '\b';
									break;
								case 't':
									*out++ = '\t';
									break;
								case 'v':
									*out++ = '\v';
									break;
								case 'f':
									*out++ = '\f';
									break;
								case 'r':
									*out++ = '\r';
									break;
								case 'n':
									*out++ = '\n';
									break;
								default:
									JSON_ASSERTF(false, "%s", "JsonParser::json_string_to_utf8: unreachable.");
								}
								++p;
							}
							else
							{
								JSON_ASSERTF(
									ch == '0' || ch == 'a' ||  ch == 'b' ||  ch == 't' ||  ch == 'v' ||  ch == 'f' ||  ch == 'r' ||  ch == 'n', 
									"%s", 
									"JsonParser::json_string_to_utf8: problem with escaped characters.");
								++p;
								++len;
							}	
						}
					}
					else // assume utf-8 encoding
					{
						if (out)
						{
							*out++ = *p++;
						}
						else
						{
							++p;
							++len;
						}
					}
				}	

				return (out) ? ((char*)out - dest) : len;
			}	

			f32 hex_to_float(const char* s)
			{
				const char* p = s;
				p += 2; // skip 0x	

				u32 x = 0;
				for (u8 count = 0; count < 8; count++)
				{
					x = x * 16 + hex_digit_to_u32(*p++);
				}	

				f32 result = 0;
				memcpy((u8*)&result, (u8*)&x, sizeof(f32));
				return result;
			}	

			f64 hex_to_double(const char* s)
			{
				const char* p = s;
				p += 2; // skip 0x	

				u32 x = 0;
				for (u8 count = 0; count < 16; count++)
				{
					x = x * 16 + hex_digit_to_u32(*p++);
				}	

				f64 result = 0;
				memcpy((u8*)&result, (u8*)&x, sizeof(f64));
				return result;
			}	

			size_type to_u64(const char* s, const char* end, u64* out) 
			{
				const char* p = s;

				u64 n = 0;
				while (p != end && *p >= '0' && *p <= '9')
				{
					n = n * 10 + (u64)(*p - '0');
					++p;
				}	

				size_t result = p - s;
				if (result > 20 || (result == 20 && memcmp(s, "18446744073709551615", 20) > 0)) 
				{
					result = 0;
				}

				*out = n;
				return result;
			}

			size_type to_s64(const char* s, const char* end, s64* out) 
			{
				const char* p = s;
				
				bool sign = false;
				if (*p == '-') 
				{
					sign = true;
					++p;
				}

				s64 n = 0;
				while (p != end && *p >= '0' && *p <= '9')
				{
					n = n * 10 + (s64)(*p - '0');
					++p;
				}	

				size_t result = p - s;
				if ((!sign && (result > 19 || (result == 19 && memcmp(s, "9223372036854775807", 19) > 0)))
					|| (sign && (result > 20 || (result == 20 && memcmp(s, "-9223372036854775808", 20) > 0)))
					) 
				{
					result = 0;
				}

				*out = sign ? -n : n;
				return result;
			}

			u32 hex_digit_to_u32(char ch)
			{
				u32 result = (u32)ch;
				if (ch >= '0' && ch <= '9')
				{
					result -= 48;
				}
				else if (ch >= 'a' && ch <= 'f')
				{
					result -= 87;
				}
				else if (ch >= 'A' && ch <= 'F')
				{
					result -= 55;
				}
				else
				{
					JSON_ASSERT(false);
				}
				return result;
			}	

		}		
    }
}

#endif // JSON_IMPLEMENTATION
