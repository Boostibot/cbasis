#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "utf.h"
#include "assert.h"

typedef int64_t isize;
typedef struct Allocator Allocator;

typedef struct Json_String {
    const char* data;
    isize count;
} Json_String;

typedef enum Json_Type {
    JSON_NULL = 0,
    JSON_NUMBER,
    JSON_STRING,
    JSON_COMMENT,
    JSON_WHITESPACE,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_BOOL,
    JSON_OBJECT_END,
    JSON_ARRAY_END,
    JSON_ERROR,
    JSON_EOF,
} Json_Type;

typedef struct Ser_Writer {
    Allocator* alloc;
    uint8_t* data;
    isize offset;
    isize capacity;
    isize depth;
    bool has_user_buffer;
} Ser_Writer;

#define JSON_READ_STRICT                0
#define JSON_READ_ALLOW_JSON5_KEYS      1
#define JSON_READ_ALLOW_JSON5_COMMENTS  2
#define JSON_READ_ALLOW_JSON5_NUMBERS   4
#define JSON_READ_ALLOW_JSON5_STRINGS   8
#define JSON_READ_ALLOW_JSON5_SPACE     16
#define JSON_READ_ALLOW_JSON5           31
typedef struct Json_Reader {
    Allocator* alloc;
    const uint8_t* data;
    isize offset;
    isize capacity;
    isize depth;

    uint32_t flags;
} Json_Reader;

typedef struct Json_Value {
    Json_Reader* r;
    Json_Type type;
    uint32_t depth;

    union {
        Json_String whitespace;
        Json_String comment;
        Json_String string_unescaped;
    };
    double number;
    bool boolean;

    uint32_t string_first_escape_at;
} Json_Value;

//json strings
Json_String json_string_of(const char* cstr);
Json_String json_string_escape(Json_String utf_string, Allocator* alloc);
Json_String json_string_unescape(Json_String json_string, Allocator* alloc);
Json_String json_string_allocate(isize size, Allocator* alloc);
void json_string_deallocate(Json_String* string, Allocator* alloc);
bool json_escaped_string_equals(Json_String json_string_with_escapes, Json_String utf8_string);
bool json_string_encode_codepoint(void* into, isize into_size, isize* offset, uint32_t codepoint);
bool json_string_decode_codepoint(const void* from, isize from_size, isize* offset, uint32_t* codepoint);

void json_write_value(Ser_Writer* r, Json_Value value);
void json_write_string(Ser_Writer* r, Json_String string);

#define JSON_READ_KEEP_WHITESPACE       32
#define JSON_READ_KEEP_COMMENTS         64
#define _JSON_READ_REMOVE_NEEDLESS_MASK (~(uint32_t)JSON_READ_KEEP_WHITESPACE & ~(uint32_t)JSON_READ_KEEP_COMMENTS)

bool json_read_value(Json_Reader* r, Json_Value* value, uint32_t flags);

bool json_key_string_equals(Json_Value val, Json_String string);
static inline bool json_key_cstring_equals(Json_Value val, const char* cstr) 
{ 
    return json_key_string_equals(val, json_string_of(cstr)); 
}


void json_read_skip_to_depth(Json_Reader* r, isize depth, uint32_t flags)
{
    Json_Value val = {0};
    while(r->depth != depth && val.type != JSON_ERROR)
        json_read_value(r, &val, flags);
}


bool json_iterate_array(const Json_Value* array, Json_Value* out_val)
{
    if(array->type != JSON_ARRAY)
        return false;
        
    json_read_skip_to_depth(array->r, array->depth, 0);
    return json_read_value(array->r, out_val, 0) && out_val->type != JSON_ARRAY_END;
}
bool json_iterate_object(const Json_Value* object, Json_Value* out_key, Json_Value* out_val)
{
    if(object->type != JSON_ARRAY)
        return false;
        
    json_read_skip_to_depth(object->r, object->depth, 0);
    if(json_read_value(object->r, out_key, 0) == false || out_key->type != JSON_STRING) //TODO: number keys?
    {
        //TODO: recovery?
        return false;
    }
        
    json_read_skip_to_depth(object->r, object->depth, 0);
    if(json_read_value(object->r, out_val, 0) == false || out_val->type == JSON_OBJECT_END)
        return false;

    return true;
}

static inline bool json_read_null(Json_Value object)  
{ 
    return object.type == JSON_NULL; 
}
static inline bool json_read_bool(Json_Value object, bool* val) 
{ 
    if(object.type == JSON_BOOL) { 
        *val = object.boolean; 
        return true;
    } 
    return false; 
}
static inline bool json_read_number(Json_Value object, double* val) 
{ 
    if(object.type == JSON_NUMBER) { 
        *val = object.number; 
        return true;
    } 
    return false; 
}
static inline bool json_read_string(Json_Value object, Json_String* val, Allocator* alloc)
{
    if(object.type == JSON_STRING) { 
        *val = json_string_unescape(object.string_unescaped, alloc);
        return true;
    } 
    return false; 
}
static inline bool json_read_comment(Json_Value object, Json_String* val)
{
    if(object.type == JSON_STRING) { 
        *val = object.comment;
        return true;
    } 
    return false; 
}

static inline bool json_read_u64(Json_Value object, uint64_t* val);
static inline bool json_read_u32(Json_Value object, uint32_t* val);
static inline bool json_read_u16(Json_Value object, uint16_t* val);
static inline bool json_read_u8(Json_Value object,  uint8_t* val);

static inline bool json_read_u64_clamp(Json_Value object, uint64_t* val, uint64_t min, uint64_t max);
static inline bool json_read_u32_clamp(Json_Value object, uint32_t* val, uint32_t min, uint32_t max);
static inline bool json_read_u16_clamp(Json_Value object, uint16_t* val, uint16_t min, uint16_t max);
static inline bool json_read_u8_clamp(Json_Value object,  uint8_t* val, uint8_t min, uint8_t max);


static inline bool json_read_f32(Json_Value object, float* val);
static inline bool json_read_f32_clamp(Json_Value object, float* val, float min, float max);

bool json_key_string_equals(Json_Value val, Json_String string)
{
    if(val.type != JSON_STRING)
        return false;

    Json_String json_string = val.string_unescaped;
    Json_String utf8_string = string;
    isize first_escape_at = val.string_first_escape_at;

    //if the utf version is shared between the two strings than we can take a fast path
    if(first_escape_at >= json_string.count || first_escape_at >= utf8_string.count)
        return json_string.count == utf8_string.count && memcmp(json_string.data, utf8_string.data, utf8_string.count) == 0;
    
    //escapes can only make the string longer so if its shorter than it cannot possibly be equal
    if(json_string.count < utf8_string.count)
        return false;
        
    if(first_escape_at < 0)
        first_escape_at = 0;
    if(memcmp(json_string.data, utf8_string.data, first_escape_at) != 0)
        return false;

    Json_String json_rest = {json_string.data + first_escape_at, json_string.count - first_escape_at};
    Json_String utf8_rest = {utf8_string.data + first_escape_at, utf8_string.count - first_escape_at};
    return json_escaped_string_equals(json_rest, utf8_rest);
}

bool json_escaped_string_equals(Json_String json_string, Json_String utf8_string)
{
    if(json_string.count < utf8_string.count)
        return false;

    isize utf8_at = 0;
    isize json_at = 0;
    while(json_at < json_string.count) {
        //find next escape or end of file
        const void* escape_ptr = memchr(json_string.data + json_at, '\\', json_string.count - json_at);
        isize escape_at = escape_ptr ? json_string.data - (const char*) escape_ptr : json_string.count;

        //compare everything between start and end of file
        isize unescaped_size = escape_at - json_at;
        if(unescaped_size > 0) {
            if(utf8_at + unescaped_size > utf8_string.count)
                return false;

            if(memcmp(json_string.data + json_at, utf8_string.data + utf8_at, unescaped_size) != 0)
                return false;
        }

        if(escape_at >= utf8_string.count)
            break;

        //Decode the two codepoints and compare them - if they dont match or one decoder failed then return not equal
        uint32_t codepoint_json = {0};
        bool decoded_json_ok = json_string_decode_codepoint(json_string.data, json_string.count, &json_at, &codepoint_json);

        uint32_t codepoint_utf8 = {0};
        bool decoded_utf8_ok = utf8_decode(utf8_string.data, utf8_string.count, &codepoint_utf8, &utf8_at);

        if(codepoint_json != codepoint_utf8 || decoded_json_ok == false || decoded_utf8_ok == false)
            return false;
    }

    return json_at == json_string.count && utf8_at == utf8_string.count;
}

typedef struct Test_Struct {
    double d1;
    double d2;
    double d3;
    float f4;
    uint32_t my_val;
} Test_Struct;

bool json_read_test_struct(const Json_Value* object, Test_Struct* out)
{
    bool ok = true;
    for(Json_Value key = {0}, val = {0}; json_iterate_object(object, &key, &val); ) {
        if(0) {}
        else if(json_key_cstring_equals(key, "d1")) ok = ok && json_read_number(val, &out->d1);
        else if(json_key_cstring_equals(key, "d2")) ok = ok && json_read_number(val, &out->d2);
        else if(json_key_cstring_equals(key, "d3")) ok = ok && json_read_number(val, &out->d3);
        else if(json_key_cstring_equals(key, "f4")) ok = ok && json_read_f32(val, &out->f4);
        else if(json_key_cstring_equals(key, "my_val")) ok = ok && json_read_u32_clamp(val, &out->my_val, 0, UINT32_MAX);
    }

    return ok && object->r->state;
}