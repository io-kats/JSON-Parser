#define _CRT_SECURE_NO_WARNINGS

#define JSON_IMPLEMENTATION
#include "json.h"

#include <cstdio>
#include <cassert>
#include <cstring>
#include <string>

// Utilities.
long get_file_length(FILE* file)
{
	if (file == nullptr)
		return 0;

	if (fseek(file, 0, SEEK_END) < 0)
		return 0;

	long file_length = ftell(file); 
	if (file_length < 0)
		return 0;

	if (fseek(file, 0, SEEK_SET) < 0)
		return 0;

	return file_length;
}

int read_file_into_buffer(FILE* file, char* read_buffer, size_t buffer_size)
{
	if (file == nullptr || read_buffer == nullptr || buffer_size == 0)
		return 0;

	size_t n = fread(read_buffer, 1, (size_t)buffer_size, file);
	if (n != buffer_size || ferror(file) < 0)
		return 0;

	return 1;
}

struct strview 
{
	const char* data;
	size_t length;
};

char big_buffer[4 * 1024 * 1024] = { 0 };
strview read_file_into_string_view(const char* filepath = nullptr) 
{
	strview file = { nullptr, 0 };
	if (filepath) 
	{
		FILE* f = fopen(filepath, "rb");
		size_t buffer_size = (size_t)get_file_length(f);
		char* buffer = &big_buffer[0];
		int rc = read_file_into_buffer(f, buffer, buffer_size);
		assert(rc != 0);
		fclose(f);
		file.data = buffer;
		file.length = buffer_size;
	}
	else
	{
		file.data = 
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
		file.length = strlen(file.data);
	}
	return file;
}

void example_with_node_buffer() 
{
	strview json_file = read_file_into_string_view("./resources/test001.json");
	printf("Example 1:\n%.*s\n\n", (unsigned int)json_file.length, json_file.data);

	// Create a node buffer.
	size_t buf_capacity = 1;
	ers::json::JsonNode* buf = new ers::json::JsonNode[buf_capacity];

	ers::json::JsonParser json_parser(json_file.data, json_file.length, &buf[0], buf_capacity);
	json_parser.Parse();

	while (json_parser.GetErrorCode() == ers::json::JsonErrorCode::CAPACITY_EXCEEDED)
	{	
		delete[] buf;
		buf_capacity *= 2;		
		buf = new ers::json::JsonNode[buf_capacity];
		printf("Re-parsing with node buffer capacity = %d...\n", (int)buf_capacity);
		json_parser.Parse(&buf[0], buf_capacity);
		printf("Is JSON valid: %s\n", json_parser.IsValid() ? "yes" : "no");
		if (!json_parser.IsValid())
		{
			printf("%s\n----------------------------\n\n", json_parser.GetErrorMessage());
		}
	}

	if (!json_parser.IsValid()) 
	{
		printf("%s\n", json_parser.GetErrorMessage());
		return;
	}
	printf("\n");

	// Inspect the nodes.
	ers::json::util::print_nodes(buf, json_parser.GetCount());
		
	const size_t node_count = json_parser.GetCount();
	printf("Buffer size: %d\n", (int)buf_capacity);
	printf("Actual count of nodes: %d\n", (int)node_count);	

	printf("\n");

	// 1st example.
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
		assert(memcmp(buf, "\"Test\"", strlen(buf)) == 0);
		printf("root%s = %s\n", path1, &buf[0]);
	}

	// 2nd example.
	const char* path2 = "[0]";
	const ers::json::JsonNode* node2 = ers::json::get_value_node(buf, path2, strlen(path2));
	assert(node2 != nullptr);
	const auto sv2 = node2->GetAsStringView();
	
	// The UTF-8 length of a string (max. 4 bytes) is always less than or equal 
	// to that of its JSON-style equivalent (at least 1 more byte for escaped characters).
	std::string str;		
	str.resize(sv2.length); // Allocate enough space for the string.
	size_t actual_length = ers::json::util::json_string_to_utf8(&str[0], sv2.data, sv2.length);
	assert(actual_length != 0 && memcmp(str.c_str(), "null", actual_length) == 0);
	str.resize(actual_length);
	printf("root%s = %s\n", path2, str.c_str());

	// 3rd example.
	const char* path3 = "[1].y";
	const ers::json::JsonNode* node3 = ers::json::get_value_node(buf, path3, strlen(path3));
	assert(node3 != nullptr);
	const ers::json::JsonNode* curr = node3->GetFirst();
	assert(curr != nullptr);
	str.resize(255);
	actual_length = curr->GetAsString(&str[0]);
	assert(actual_length != 0 && memcmp(str.c_str(), "Test", actual_length) == 0);
	str.resize(actual_length);
	printf("root%s[0] = %s\n", path3, str.c_str());
	
	curr = curr->GetNext();
	assert(curr);
	float y1;
	int rc = curr->GetAsFloat(&y1);
	assert(rc == 1 && y1 == 3.14f);
	printf("root%s[1] = %.9g\n", path3, y1);
	assert(curr->GetNext() == nullptr);

	int64_t n;
	const char* path4 = "[2][-1]";
	const ers::json::JsonNode* node4 = ers::json::get_value_node(buf, path4, strlen(path4));
	rc = node4->GetAsS64(&n);
	assert(rc == 1 && n == INT64_MIN);
	printf("root%s = %zd\n", path4, n);

	assert(curr->GetNext() == nullptr);

	delete[] buf;
	
	printf("\n\n-------------------------------------------------\n\n");
}

void example_with_flat_json() 
{
	strview json_file = read_file_into_string_view("./resources/test002.json");
	printf("Example 2:\n%.*s\n\n", (unsigned int)json_file.length, json_file.data);

	// Create a FlatJson instance.
	ers::json::FlatJson<64> flat_json;

	ers::json::JsonParser json_parser(json_file.data, json_file.length);
	json_parser.Parse(flat_json);

	if (!json_parser.IsValid()) 
	{
		printf("%s\n", json_parser.GetErrorMessage());
		return;
	}
	printf("\n");

	const size_t node_count = flat_json.GetCount();
	printf("Buffer size: %d\n", (int)flat_json.GetCapacity());
	printf("Count of nodes: %d\n", (int)node_count);

	// Inspect the nodes.
	ers::json::util::print_nodes(&flat_json[0], node_count);	

	// 1st example.
	const char* path = ".Inhaber.Alter";
	const ers::json::JsonNode* node1 = flat_json.GetValueNode(path);
	assert(node1 != nullptr);
	double d = 0.0;
	int rc = node1->GetAsDouble(&d);
	assert(rc != 0 && d == 42.0);
	printf("root%s = %.17g\n", path, d);

	// 2nd example.
	// Allocate enough space for the string.
	std::string str;
	str.resize(255);
	size_t actual_length = flat_json.GetAsString(".Nummer", &str[0]); 
	assert(actual_length != 0 && memcmp(str.c_str(), "1234-5678-9012-3456", actual_length) == 0);
	str.resize(actual_length);
	printf("root.Nummer = %s\n", str.c_str());

	// 3rd example.
	const ers::json::JsonNode* node3 = flat_json.GetValueNode(".Inhaber");
	bool maennlich;
	rc = flat_json.GetAsBool(".maennlich", &maennlich, node3); 
	assert(rc != 0 && maennlich);	
	printf("root.Inhaber.maennlich = %s\n", maennlich ? "true" : "false");

	printf("\n\n-------------------------------------------------\n\n");
}

#include <random>
struct Point3
{
	double x, y, z;
};

// Serialize random 3d points to a JSON file, while writing properties 
// in a random order and caching the points into the input_points array.
// Then deserialize them and compare with the original ones. Testing
// small string optimization, iterating an object in a FlatJson for
// deserialization as well as whether the order of the properties of an object
// plays a role (it should not).
void test_serialize_and_deserialize_simple_random_generated_json_array()
{
	printf("Test:\n");

	// Setup.
	int random_indices[3] = { 0, 1, 2 };
	std::random_device rd;
	std::mt19937 gen(rd());
	
	auto random_integer = [&](int min, int max) -> int
	{	
		std::uniform_int_distribution<> distr(min, max);
		return distr(gen);
	};	

	auto randomize_indices = [&]() -> void
	{	
		for (int i = 0; i < 3; ++i)
		{
			std::uniform_int_distribution<> distr(i, 2);
			int rand_idx = distr(gen);

			int temp = random_indices[i];
			random_indices[i] = random_indices[rand_idx];
			random_indices[rand_idx] = temp;
		}
	};	

	// Fill input_points array.
	const size_t point_count = random_integer(10, 30);		
	Point3* input_points = new Point3[point_count];
	for (size_t i = 0; i < point_count; ++i)
	{
		input_points[i].x = (double)random_integer(0, 128);
		input_points[i].y = (double)random_integer(0, 128);
		input_points[i].z = (double)random_integer(0, 128);
	}

	// Serialize random 3d points.
	char test_json_string[8 * 1024] = { 0 };
	char* p = &test_json_string[0];

	*p++ = '[';
	*p++ = '\n';
	for (size_t i = 0; i < point_count; ++i)
	{
		randomize_indices();

		*p++ = '\t';
		*p++ = '{';
		*p++ = '\n';		
		for (int k = 0; k < 3; ++k)
		{
			if (random_indices[k] == 0) 
			{
				p += sprintf(p, "\t\t\"x\": %.17g", input_points[i].x);
			}
			else if (random_indices[k] == 1) 
			{
				p += sprintf(p, "\t\t\"y\": %.17g", input_points[i].y);
			}
			else if (random_indices[k] == 2) 
			{
				p += sprintf(p, "\t\t\"z\": %.17g", input_points[i].z);
			}

			if (k < 2)
			{
				*p++ = ',';
			}
			*p++ = '\n';
		}
		*p++ = '\t';
		*p++ = '}';

		if (i < point_count - 1)
		{
			*p++ = ',';
		}
		*p++ = '\n';
	}
	*p++ = ']';
	*p++ = '\n';

	printf("%s\n\n", test_json_string);

	// Parse the json file to make sure it is valid.
	ers::json::FlatJson<1024> flat_json;
	const char* json_file = &test_json_string[0];
	ers::json::JsonParser json_parser(json_file, strlen(json_file));
	json_parser.Parse(flat_json);	
	
	JSON_ASSERTF(json_parser.IsValid(), "%s", "Serialization was not successful, validation failed.");
	
	// Deserialise the points into the output_points array and compare them to the input ones.
	Point3* output_points = new Point3[point_count];
	const ers::json::JsonNode* current_object = flat_json.GetBegin().GetFirst();	
	size_t j = 0;
	while (current_object)
	{
		Point3& point = output_points[j++];
		const ers::json::JsonNode* current_property = current_object->GetFirst();
		while (current_property)
		{
			JSON_ASSERTF(current_property->type == ers::json::JsonNodeType::JSON_KEY, "%s", "Current property key node is not a key.");

			const auto key = current_property->GetAsStringView();
			JSON_ASSERTF(key.length == 3, "%s", "Key length is incorrect.");

			const ers::json::JsonNode* current_value = current_property->GetValue();
			JSON_ASSERTF(current_value->type == ers::json::JsonNodeType::JSON_NUMBER, "%s", "Value is not a number.");

			int result = 0;
			if (memcmp(key.data, "\"x\"", key.length) == 0)
			{
				result = current_value->GetAsDouble(&point.x);			
			}
			else if (memcmp(key.data, "\"y\"", key.length) == 0)
			{
				result = current_value->GetAsDouble(&point.y);			
			}
			else if (memcmp(key.data, "\"z\"", key.length) == 0)
			{
				result = current_value->GetAsDouble(&point.z);			
			}
			JSON_ASSERTF(result == 1, "%s", "Could not parse a 3d point object.");

			current_property = current_property->GetNext();
		}
		current_object = current_object->GetNext();
	}
	JSON_ASSERTF(j == point_count, "%s", "Number of deserialized objects is not correct.");

	for (size_t i = 0; i < point_count; ++i)
	{
		JSON_ASSERTF(input_points[i].x == output_points[i].x, "%s", "The x-coordinate isn't correct.");
		JSON_ASSERTF(input_points[i].y == output_points[i].y, "%s", "The y-coordinate isn't correct.");
		JSON_ASSERTF(input_points[i].z == output_points[i].z, "%s", "The z-coordinate isn't correct.");
	}

	printf("TEST SUCCESSFUL.\n\n");

	delete[] input_points;
	delete[] output_points;

	printf("\n\n-------------------------------------------------\n\n");
}

int64_t read_input()
{	
	printf(
		"1. Example with small node buffer\n"
		"2. Example using a FlatJson instance\n"
		"3. Test for serializing and deserializing a simple JSON array\n"
		"4. Close program\n"
		"> "
	);
	fflush(stdout);	

	char buf[512] = { 0 };
	int i = 0;
	char ch = getchar();
	while (ch != EOF && ch != '\n') 
	{
		if (ch >= '0' && ch <= '9')
		{
			buf[i++] = ch;
		}
		ch = getchar();
	}

	int64_t result = 0;
	size_t ec = ers::json::util::to_s64(&buf[0], &buf[i], &result);
	if (ec == 0)
	{
		result = -1;
	}
	return result;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	
	long choice = -1;
	while (choice != 4) 
	{		
		choice = read_input();
		switch (choice)
		{
		case 1:
			example_with_node_buffer();
			break;
		case 2:
			example_with_flat_json();
			break;
		case 3:
			test_serialize_and_deserialize_simple_random_generated_json_array();
			break;
		default:
			break;
		}
	}

	return 0;
}
