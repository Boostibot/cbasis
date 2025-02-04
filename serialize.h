#include <stdint.h>
#include <stdbool.h>
#include "string.h"

//based on https://rxi.github.io/a_simple_serialization_system.html
typedef enum Ser_Type {
    SER_NULL = 0,
    
    //we include "recovery" lists/object. These act just like the regular ones
    // except also contain a tag - some magic number or string which allows
    // us to recover in case of file corruption. Whats nice about this is that
    // this mechanism can be made entirely transparent to the reader and very
    // hustle free for the writer.
    SER_LIST_BEGIN,
    SER_OBJECT_BEGIN,
    SER_RECOVERY_OBJECT_BEGIN,  //{u8 type, u8 size}[size bytes of tag]\0
    SER_RECOVERY_LIST_BEGIN,  //{u8 type, u8 size}[size bytes of tag]\0
    
    SER_LIST_END,
    SER_OBJECT_END,
    SER_RECOVERY_LIST_END,    //{u8 type, u8 size}[size bytes of tag]\0
    SER_RECOVERY_OBJECT_END,    //{u8 type, u8 size}[size bytes of tag]\0
    SER_ERROR, //"lexing" error. Is near the ENDers section so that we can check for ender or error efficiently 

    SER_STRING_0,  //{u8 type}
    SER_STRING_8,  //{u8 type, u8 size}[size bytes]\0
    SER_STRING_64, //{u8 type, u64 size}[size bytes]\0

    SER_BINARY_0,  //{u8 type}
    SER_BINARY_8,  //{u8 type, u8 size}[size bytes]
    SER_BINARY_64, //{u8 type, u64 size}[size bytes]

    SER_BOOL,

    SER_U8,
    SER_U16,
    SER_U32,
    SER_U64,
    
    SER_I8,
    SER_I16,
    SER_I32,
    SER_I64,
    
    SER_F8,
    SER_F16,
    SER_F32,
    SER_F64,

    SER_F32V2,
    SER_F32V3,
    SER_F32V4,
    
    SER_I32V2,
    SER_I32V3,
    SER_I32V4,


    //aliases
    SER_LIST = SER_LIST_BEGIN,
    SER_OBJECT = SER_OBJECT_BEGIN,
    SER_RECOVERY_LIST = SER_RECOVERY_LIST_BEGIN,
    SER_RECOVERY_OBJECT = SER_RECOVERY_OBJECT_BEGIN,
    SER_STRING = SER_STRING_64,
    SER_BINARY = SER_BINARY_64,
    SER_DYN_COUNT = 4,
} Ser_Type;

typedef struct Ser_Writer {
    void (*write)(void* context, const void* data, isize size);
    void* context;
} Ser_Writer;

typedef struct Ser_Reader {
    const uint8_t* data;
    isize depth;
    isize offset;
    isize capacity;

    isize error_count;
    isize recovery_count;
    void (*error_log)(void* context, isize depth, isize offset, const char* fmt, ...);
    void* error_log_context;
} Ser_Reader;

#if 1
    typedef String Ser_String;
#else
    typedef struct Ser_String {
        const char* data;
        isize count;
    } Ser_String;
#endif

typedef struct Ser_Value {
    Ser_Reader* context;

    isize depth;
    isize offset;
    Ser_Type exact_type;
    Ser_Type type;
    union {
        Ser_String binary;
        Ser_String string;
        bool vbool;

        f32 f64v4[4];
        i32 i64v4[4];
        f32 f32v4[4];
        i32 i32v4[4];

        i64 i64;
        u64 u64;
        f64 f64;
        f32 f32;
    };
} Ser_Value;

isize set_type_size(Ser_Type type);
const char* set_type_name(Ser_Type type);
Ser_Type ser_type_category(Ser_Type type);
bool ser_type_is_numeric(Ser_Type type);
bool ser_type_is_integer(Ser_Type type);
bool ser_type_is_signed_integer(Ser_Type type);
bool ser_type_is_unsigned_integer(Ser_Type type);
bool ser_type_is_float(Ser_Type type);

Ser_Writer ser_file_writer(FILE* file);
void ser_write(Ser_Writer* context, const void* ptr, isize size);

void ser_section_begin(Ser_Writer* context, const void* ptr, isize size);
void ser_section_end(Ser_Writer* context, const void* ptr, isize size);

void ser_list_begin(Ser_Writer* context);
void ser_list_end(Ser_Writer* context);

void ser_object_begin(Ser_Writer* context);
void ser_object_end(Ser_Writer* context);

void ser_primitive(Ser_Writer* context, Ser_Type type, const void* ptr, isize size);
void ser_binary(Ser_Writer* context, const void* ptr, isize size);
void ser_string(Ser_Writer* context, const void* ptr, isize size);
void ser_null(Ser_Writer* context);
void ser_bool(Ser_Writer* context, bool val);

void ser_i8(Ser_Writer* context, i8 val);
void ser_i16(Ser_Writer* context, i16 val);
void ser_i32(Ser_Writer* context, i32 val);
void ser_i64(Ser_Writer* context, i64 val);

void ser_u8(Ser_Writer* context, u8 val);
void ser_u16(Ser_Writer* context, u16 val);
void ser_u32(Ser_Writer* context, u32 val);
void ser_u64(Ser_Writer* context, u64 val);

void ser_f32(Ser_Writer* context, f32 val);
void ser_f64(Ser_Writer* context, f64 val);

void ser_i32v2(Ser_Writer* context, const i32 vals[2]);
void ser_i32v3(Ser_Writer* context, const i32 vals[3]);
void ser_i32v4(Ser_Writer* context, const i32 vals[4]);

void ser_f32v2(Ser_Writer* context, const f32 vals[2]);
void ser_f32v3(Ser_Writer* context, const f32 vals[3]);
void ser_f32v4(Ser_Writer* context, const f32 vals[4]);


//reading 
bool deser_read(Ser_Reader* context, void* ptr, isize size);
Ser_Value deser_value(Ser_Reader* context);


#define ser_cstring_eq(value, cstr) ser_string_eq(value, STRING(cstr))
bool ser_string_eq(Ser_Value value, String str);

//IMPL
isize set_type_size(Ser_Type type)          {return 0;}
const char* set_type_name(Ser_Type type)    {return "";}

Ser_Type ser_type_category(Ser_Type type)
{
    switch (type)
    {
        case SER_U8: return SER_U64;
        case SER_U16: return SER_U64;
        case SER_U32: return SER_U64;
        case SER_U64: return SER_U64;
        
        case SER_I8: return SER_I64;
        case SER_I16: return SER_I64;
        case SER_I32: return SER_I64;
        case SER_I64: return SER_I64;
        
        case SER_F8: return SER_F64;
        case SER_F16: return SER_F64;
        case SER_F32: return SER_F64;
        case SER_F64: return SER_F64;
        default: return type;
    }
}

bool ser_type_is_numeric(Ser_Type type)             { return SER_U8 <= (int) type && (int) type <= SER_F64; }
bool ser_type_is_integer(Ser_Type type)             { return SER_U8 <= (int) type && (int) type <= SER_I64; }
bool ser_type_is_signed_integer(Ser_Type type)      { return SER_I8 <= (int) type && (int) type <= SER_I64; }
bool ser_type_is_unsigned_integer(Ser_Type type)    { return SER_U8 <= (int) type && (int) type <= SER_U64; }
bool ser_type_is_float(Ser_Type type)               { return SER_F8 <= (int) type && (int) type <= SER_F64; }

inline static
void ser_primitive(Ser_Writer* context, Ser_Type type, const void* ptr, isize size)
{
    struct Temp {
        uint8_t type;
        uint8_t values[16];          
    } temp;
    
    temp.type = (uint8_t) type;
    memcpy(temp.values, ptr, size);
    ser_write(context, &temp, 1 + sizeof size);
}

inline static void _ser_binary_or_string(Ser_Writer* context, const void* ptr, isize size, bool is_string)
{
    if(size <= 0)
        ser_primitive(context, is_string ? SER_STRING_0 : SER_BINARY_0, NULL, 0);
    else
    {
        if(size >= 256) 
            ser_primitive(context, is_string ? SER_STRING_64 : SER_BINARY_64, &size, sizeof size);
        else {
            uint8_t _size = (uint8_t) size;
            ser_primitive(context, is_string ? SER_STRING_8 : SER_BINARY_8, &_size, sizeof _size);
        }

        ser_write(context, ptr, size);
        uint8_t null = 0;
        if(is_string) 
            ser_write(context, &null, sizeof null);
    }
}

void _ser_recovery(Ser_Writer* context, Ser_Type type, const void* ptr, isize size)
{
    uint8_t null = 0;
    if(size < 0)
        size = ptr ? strlen((const char*) ptr) : 0;

    uint8_t usize = (uint8_t) size;
    ser_primitive(context, type, &usize, size);
    ser_write(context, ptr, size);
    ser_write(context, &null, sizeof null);
}

void ser_null(Ser_Writer* context)           { ser_primitive(context, SER_I8, NULL, 0); }
void ser_bool(Ser_Writer* context, bool val) { ser_primitive(context, SER_I8,  &val, sizeof val); }

void ser_i8(Ser_Writer* context, i8 val)     { ser_primitive(context, SER_I8,  &val, sizeof val); }
void ser_i16(Ser_Writer* context, i16 val)   { ser_primitive(context, SER_I16, &val, sizeof val); }
void ser_i32(Ser_Writer* context, i32 val)   { ser_primitive(context, SER_I32, &val, sizeof val); }
void ser_i64(Ser_Writer* context, i64 val)   { ser_primitive(context, SER_I64, &val, sizeof val); }

void ser_u8(Ser_Writer* context, u8 val)     { ser_primitive(context, SER_U8,  &val, sizeof val); }
void ser_u16(Ser_Writer* context, u16 val)   { ser_primitive(context, SER_U16, &val, sizeof val); }
void ser_u32(Ser_Writer* context, u32 val)   { ser_primitive(context, SER_U32, &val, sizeof val); }
void ser_u64(Ser_Writer* context, u64 val)   { ser_primitive(context, SER_U64, &val, sizeof val); }

void ser_f32(Ser_Writer* context, f32 val)   { ser_primitive(context, SER_F32, &val, sizeof val); }
void ser_f64(Ser_Writer* context, f64 val)   { ser_primitive(context, SER_F64, &val, sizeof val); }

void ser_list_begin(Ser_Writer* context)     { ser_primitive(context, SER_LIST_BEGIN, NULL, 0); }
void ser_list_end(Ser_Writer* context)       { ser_primitive(context, SER_LIST_END, NULL, 0); }
void ser_object_begin(Ser_Writer* context)   { ser_primitive(context, SER_OBJECT_BEGIN, NULL, 0); }
void ser_object_end(Ser_Writer* context)     { ser_primitive(context, SER_OBJECT_END, NULL, 0); }

void ser_recovery_list_begin(Ser_Writer* context, const void* ptr, isize size)      { _ser_recovery(context, SER_RECOVERY_LIST_BEGIN, ptr, size); }
void ser_recovery_list_end(Ser_Writer* context, const void* ptr, isize size)        { _ser_recovery(context, SER_RECOVERY_LIST_END, ptr, size); }
void ser_recovery_object_begin(Ser_Writer* context, const void* ptr, isize size)    { _ser_recovery(context, SER_RECOVERY_OBJECT_BEGIN, ptr, size); }
void ser_recovery_object_end(Ser_Writer* context, const void* ptr, isize size)      { _ser_recovery(context, SER_RECOVERY_OBJECT_END, ptr, size); }

void ser_i32v2(Ser_Writer* context, const i32 vals[2]) { ser_primitive(context, SER_I32V2, &vals, sizeof vals); }
void ser_i32v3(Ser_Writer* context, const i32 vals[3]) { ser_primitive(context, SER_I32V3, &vals, sizeof vals); }
void ser_i32v4(Ser_Writer* context, const i32 vals[4]) { ser_primitive(context, SER_I32V4, &vals, sizeof vals); }

void ser_f32v2(Ser_Writer* context, const f32 vals[2]) { ser_primitive(context, SER_F32V2, &vals, sizeof vals); }
void ser_f32v3(Ser_Writer* context, const f32 vals[3]) { ser_primitive(context, SER_F32V3, &vals, sizeof vals); }
void ser_f32v4(Ser_Writer* context, const f32 vals[4]) { ser_primitive(context, SER_F32V4, &vals, sizeof vals); }

void ser_binary(Ser_Writer* context, const void* ptr, isize size) { _ser_binary_or_string(context, ptr, size, true); }
void ser_string(Ser_Writer* context, const void* ptr, isize size) { _ser_binary_or_string(context, ptr, size, true); }
void ser_cstring(Ser_Writer* context, const char* ptr)            { _ser_binary_or_string(context, ptr, ptr ? strlen(ptr) : 0, true); }

//@TODO: inspect assembly. Perhaps can be more efficient if we use pointers instead of offsets?
bool deser_read(Ser_Reader* context, void* ptr, isize size)
{
    if(context->offset + size > context->capacity)
        return false;

    memcpy(ptr, context->data + context->offset, size);
    context->offset += size;
    return true;
}

bool deser_skip(Ser_Reader* context, isize size)
{
    if(context->offset + size > context->capacity)
        return false;

    context->offset += size;
    return true;
}

Ser_Value deser_value(Ser_Reader* context)
{
    Ser_Value out = {0};
    out.type = SER_ERROR;
    out.exact_type = SER_ERROR;
    out.offset = context->offset;
    out.depth = context->depth;

    uint8_t uncast_type = 0; 
    uint8_t ok = true;
    if(deser_read(context, &uncast_type, sizeof uncast_type))
    {
        Ser_Type type = (Ser_Type) uncast_type;
        out.exact_type = type;
        switch (type)
        {
            case SER_NULL: { out.type = SER_NULL; } break;
            case SER_BOOL: { out.type = deser_read(context, &out.vbool, 1) ? SER_ERROR : type; } break;

            case SER_U8:  { ok = deser_read(context, &out.u64, 1); out.type = SER_I64; } break;
            case SER_U16: { ok = deser_read(context, &out.u64, 2); out.type = SER_I64; } break;
            case SER_U32: { ok = deser_read(context, &out.u64, 4); out.type = SER_I64; } break;
            case SER_U64: { ok = deser_read(context, &out.u64, 8); out.type = SER_I64; } break;
            
            case SER_I8:  { i8  val = 0; ok = deser_read(context, &val, 1); out.i64 = val; out.type = SER_I64; } break;
            case SER_I16: { i16 val = 0; ok = deser_read(context, &val, 2); out.i64 = val; out.type = SER_I64; } break;
            case SER_I32: { i32 val = 0; ok = deser_read(context, &val, 4); out.i64 = val; out.type = SER_I64; } break;
            case SER_I64: { i64 val = 0; ok = deser_read(context, &val, 8); out.i64 = val; out.type = SER_I64; } break;
            
            case SER_F8:  { u8  val = 0; ok = deser_read(context, &val, 1); out.f64 = val; out.type = SER_F64; } break;
            case SER_F16: { u16 val = 0; ok = deser_read(context, &val, 2); out.f64 = val; out.type = SER_F64; } break;
            case SER_F32: { f32 val = 0; ok = deser_read(context, &val, 4); out.f64 = val; out.type = SER_F64; } break;
            case SER_F64: { f64 val = 0; ok = deser_read(context, &val, 8); out.f64 = val; out.type = SER_F64; } break;

            case SER_F32V2: { ok = deser_read(context, &out.f32v4, 2*sizeof out.f32); out.type = type; } break;
            case SER_F32V3: { ok = deser_read(context, &out.f32v4, 3*sizeof out.f32); out.type = type; } break;
            case SER_F32V4: { ok = deser_read(context, &out.f32v4, 4*sizeof out.f32); out.type = type; } break;
            
            case SER_I32V2: { ok = deser_read(context, &out.i32v4, 8); out.type = type; } break;
            case SER_I32V3: { ok = deser_read(context, &out.i32v4, 12); out.type = type; } break;
            case SER_I32V4: { ok = deser_read(context, &out.i32v4, 16); out.type = type; } break;

            case SER_LIST_END:
            case SER_OBJECT_END:    { out.type = type; context->depth -= 1; } break;

            case SER_LIST_BEGIN:    
            case SER_OBJECT_BEGIN:  { out.type = type; context->depth += 1; } break;

            case SER_RECOVERY_LIST_END:
            case SER_RECOVERY_OBJECT_END:    
            case SER_RECOVERY_LIST_BEGIN:    
            case SER_RECOVERY_OBJECT_BEGIN:  { 
                uint8_t size = 0;
                uint8_t null = 0;
                ok &= deser_read(context, &size, sizeof size);
                out.string.data = (char*) (void*) (context->data + context->offset);
                out.string.count = size;
                ok &= deser_skip(context, out.string.count);
                ok &= deser_read(context, &null, sizeof null);
                ok &= null == 0;
                out.type = type; 
                if(ok) {
                    if((uint32_t) type - SER_LIST_END < SER_DYN_COUNT)
                        context->depth -= 1; 
                    else
                        context->depth += 1; 
                }
            } break;

            //TODO: string and binary can probably be merged in their cases?
            //TODO: keep just binary 64?
            case SER_STRING_0:  { 
                out.type = SER_STRING; 
                out.string.data = "";
                out.string.count = 0;
            } break;
            case SER_STRING_8:
            case SER_STRING_64:  { 
                uint8_t null = 0;
                uint8_t size = 0;
                out.type = SER_STRING;
                if(type == SER_STRING_64) 
                    ok &= deser_read(context, &out.string.count, sizeof out.string.count);
                else {
                    ok &= deser_read(context, &size, sizeof size);
                    out.string.count = size;
                }
                
                out.string.data = (char*) (void*) (context->data + context->offset);
                
                ok &= deser_skip(context, out.string.count);
                ok &= deser_read(context, &null, sizeof null);
                ok &= null == 0;
            } break;

            case SER_BINARY_0:  { 
                out.type = SER_BINARY; 
                out.binary.data = "";
                out.binary.count = 0;
            } break;
            case SER_BINARY_8:
            case SER_BINARY_64:  { 
                out.type = SER_BINARY;
                if(type == SER_BINARY_64) 
                    ok &= deser_read(context, &out.binary.count, sizeof out.binary.count);
                else {
                    uint8_t size = 0;
                    ok &= deser_read(context, &size, sizeof size);
                    out.binary.count = size;
                }
                
                out.binary.data = (char*) (void*) (context->data + context->offset);
                ok &= deser_skip(context, out.binary.count);
            } break;
            
            default: {
                ok = false;
                //do error
            } break;
        }

    }

    if(ok == false) {
        out.type = SER_ERROR;
        context->offset = out.offset;
    }

    return out;
}

//@TODO: optimize
void deser_skip_to_depth(Ser_Reader* context, isize depth)
{
    Ser_Value val = {0};
    while(val.type != SER_ERROR && context->depth != depth)
        val = deser_value(context);
}

bool _deser_recover(Ser_Value object);

bool ser_type_is_ender(Ser_Type type)
{
    return (uint32_t) type - SER_LIST_END < SER_DYN_COUNT;
}

bool ser_type_is_ender_or_error(Ser_Type type)
{
    return (uint32_t) type - SER_LIST_END < SER_DYN_COUNT || type == SER_ERROR;
}

bool deser_iterate_list(Ser_Value list, Ser_Value* out_val)
{
    ASSERT(list.type == SER_LIST || list.type == SER_RECOVERY_LIST);
    deser_skip_to_depth(list.context, list.depth);
    *out_val = deser_value(list.context);
    if(ser_type_is_ender_or_error(out_val->type))
    {
        if(list.type != out_val->type - SER_DYN_COUNT)
            _deser_recover(list);
        return false;
    }

    return true;
}


bool deser_iterate_object(Ser_Value object, Ser_Value* out_key, Ser_Value* out_val)
{
    ASSERT(object.type == SER_OBJECT || object.type == SER_RECOVERY_OBJECT);

    deser_skip_to_depth(object.context, object.depth);
    *out_key = deser_value(object.context);
    if(ser_type_is_ender_or_error(out_key->type)) 
    {
        //if the ending type does not correspond to the object type
        if(object.type != out_key->type - SER_DYN_COUNT)
            goto recover;
        return false;
    }

    //NOTE: can be removed if we disallow dynamic as keys
    // then this case will just full under error.
    deser_skip_to_depth(object.context, object.depth); 
    *out_val = deser_value(object.context);
    if(ser_type_is_ender_or_error(out_key->type))
        goto recover;

    return true;

    recover:
    _deser_recover(object);
    return false;
}

static isize _ser_find_first_or(String in_str, String search_for, isize from, isize if_not_found)
{
    ASSERT(from >= 0);
    if(from + search_for.count > in_str.count)
        return if_not_found;
    
    if(search_for.count == 0)
        return from;

    if(search_for.count == 1)
        return string_find_first_char(in_str, search_for.data[0], from);

    const char* found = in_str.data + from;
    char last_char = search_for.data[search_for.count - 1];
    char first_char = search_for.data[0];

    while (true)
    {
        isize remaining_length = in_str.count - (found - in_str.data) - search_for.count + 1;
        ASSERT(remaining_length >= 0);

        found = (const char*) memchr(found, first_char, (size_t) remaining_length);
        if(found == NULL)
            return if_not_found;
            
        char last_char_of_found = found[search_for.count - 1];
        if (last_char_of_found == last_char)
            if (memcmp(found + 1, search_for.data + 1, (size_t) search_for.count - 2) == 0)
                return found - in_str.data;

        found += 1;
    }

    return if_not_found;
}

static bool _deser_recover(Ser_Value object)
{
    Ser_Reader* reader = object.context;
    
    isize recovery_len = 1;
    char recovery_text[270];
    if(object.type == SER_LIST)
        recovery_text[0] = SER_LIST_END;
    else if(object.type == SER_OBJECT)
        recovery_text[0] = SER_OBJECT_END;
    else if(object.type == SER_RECOVERY_LIST || object.type == SER_RECOVERY_OBJECT)
    {
        isize i = 0;
        recovery_text[i++] = object.type == SER_RECOVERY_LIST ? SER_RECOVERY_LIST_END : SER_RECOVERY_OBJECT_END;
        recovery_text[i++] = (uint8_t) object.string.count;
        memcpy(recovery_text + 2, object.string.data, object.string.count); i += object.string.count + 2;
        recovery_text[i++] = '\0';
        recovery_len = i;
    }
    else
        ASSERT(false);

    String total = {(char*) reader->data, reader->capacity};
    String recovery = {recovery_text, recovery_len};
    isize recovered = _ser_find_first_or(total, recovery, reader->offset, -1);
    if(recovered != -1) {
        reader->offset = recovered;
        printf("recovered!\n");
    }

    return recovered != -1;
}

#define string_is_equalc(string, constant_string) string_is_equal(string, STRING(constant_string))
bool ser_string_eq(Ser_Value value, String str)
{
    return value.type == SER_STRING && string_is_equal(value.string, str);
}
bool deser_f32(f32* val, Ser_Value object) { if(object.type == SER_F64) { *val = (f32) object.f64; return true; } return false; }
bool deser_f64(f64* val, Ser_Value object) { if(object.type == SER_F64) { *val = (f64) object.f64; return true; } return false; }

bool deser_null(Ser_Value object)                { return object.type == SER_NULL; }
bool deser_bool(bool* val, Ser_Value object)     { if(object.type == SER_BOOL) { *val = object.vbool; return true; } return false; }
bool deser_binary(String* val, Ser_Value object) { if(object.type == SER_STRING) { *val = object.string; return true; } return false; }
bool deser_string(String* val, Ser_Value object) { if(object.type == SER_BINARY) { *val = object.binary; return true; } return false; }

bool deser_i8(i8* val, Ser_Value object)   { if(object.type == SER_I64) { *val = (i8)  object.i64; return true; } return false; }
bool deser_i16(i16* val, Ser_Value object) { if(object.type == SER_I64) { *val = (i16) object.i64; return true; } return false; }
bool deser_i32(i32* val, Ser_Value object) { if(object.type == SER_I64) { *val = (i32) object.i64; return true; } return false; }
bool deser_i64(i64* val, Ser_Value object) { if(object.type == SER_I64) { *val = (i64) object.i64; return true; } return false; }

bool deser_u8(i8* val, Ser_Value object)   { if(object.type == SER_U64) { *val = (u8)  object.u64; return true; } return false; }
bool deser_u16(i16* val, Ser_Value object) { if(object.type == SER_U64) { *val = (u16) object.u64; return true; } return false; }
bool deser_u32(i32* val, Ser_Value object) { if(object.type == SER_U64) { *val = (u32) object.u64; return true; } return false; }
bool deser_u64(i64* val, Ser_Value object) { if(object.type == SER_U64) { *val = (u64) object.u64; return true; } return false; }

bool deser_f32v3(float out[3], Ser_Value object)
{
    if(object.type == SER_F32V3 || object.type == SER_F32V4)
    {
        out[0] = object.f32v4[0];
        out[1] = object.f32v4[1];
        out[2] = object.f32v4[2]; 
        return true;
    }
    else if(object.type == SER_OBJECT)
    {
        int parts = 0;
        for(Ser_Value key = {0}, val = {0}; deser_iterate_object(object, &key, &val); ) 
        {
                 if(ser_cstring_eq(key, "x")) parts |= deser_f32(&out[0], val) << 0;
            else if(ser_cstring_eq(key, "y")) parts |= deser_f32(&out[1], val) << 1;
            else if(ser_cstring_eq(key, "z")) parts |= deser_f32(&out[2], val) << 2;
        }

        return parts == 7;
    }
    else if(object.type == SER_LIST)
    {
        int count = 0;
        for(Ser_Value val = {0}; deser_iterate_list(object, &val); ) {
            count += deser_f32(&out[count], val);
            if(count >= 3)
                break;
        }

        return count >= 3;
    }
    return false;    
}


#include "math.h"
typedef enum Map_Scale_Filter {
    MAP_SCALE_FILTER_BILINEAR = 0,
    MAP_SCALE_FILTER_TRILINEAR,
    MAP_SCALE_FILTER_NEAREST,
} Map_Scale_Filter;

typedef enum Map_Repeat {
    MAP_REPEAT_REPEAT = 0,
    MAP_REPEAT_MIRRORED_REPEAT,
    MAP_REPEAT_CLAMP_TO_EDGE,
    MAP_REPEAT_CLAMP_TO_BORDER
} Map_Repeat;

#define MAX_CHANNELS 4
typedef struct Map_Info {
    Vec3 offset;                
    Vec3 scale; //default to 1 1 1
    Vec3 resolution;

    i32 channels_count; //the number of channels this texture should have. Is in range [0, MAX_CHANNELS] 
    i32 channels_idices1[MAX_CHANNELS]; //One based indices into the image channels. 

    Map_Scale_Filter filter_minify;
    Map_Scale_Filter filter_magnify;
    Map_Repeat repeat_u;
    Map_Repeat repeat_v;
    Map_Repeat repeat_w;

    f32 gamma;          //default 2.2
    f32 brightness;     //default 0
    f32 contrast;       //default 0
} Map_Info;

bool deser_map_repeat(Map_Repeat* repeat, Ser_Value val)
{
    if(0) {}
    else if(ser_cstring_eq(val, "repeat"))          *repeat = MAP_REPEAT_REPEAT;
    else if(ser_cstring_eq(val, "mirrored"))        *repeat = MAP_REPEAT_MIRRORED_REPEAT;
    else if(ser_cstring_eq(val, "clamp_to_edge"))   *repeat = MAP_REPEAT_CLAMP_TO_EDGE;
    else if(ser_cstring_eq(val, "clamp_to_border")) *repeat = MAP_REPEAT_CLAMP_TO_BORDER;
    else return false; //log here
    return true;
}

bool deser_map_scale_filter(Map_Scale_Filter* filter, Ser_Value val)
{
    if(0) {}
    else if(ser_cstring_eq(val, "bilinear")) *filter = MAP_SCALE_FILTER_BILINEAR;
    else if(ser_cstring_eq(val, "trilinear"))*filter = MAP_SCALE_FILTER_TRILINEAR;
    else if(ser_cstring_eq(val, "nearest"))  *filter = MAP_SCALE_FILTER_NEAREST;
    else return false; //log here
    return true;
}

bool deser_map_info(Map_Info* out_map_info, Ser_Value object)
{
    Map_Info out = {0};
    out.scale = vec3_of(1);
    out.gamma = 2.2f;

    if(object.type != SER_OBJECT) 
        return false;

    for(Ser_Value key = {0}, val = {0}; deser_iterate_object(object, &key, &val); )
    {
        /**/ if(ser_cstring_eq(key, "offset"))          deser_f32v3(out.offset.floats, val);
        else if(ser_cstring_eq(key, "scale"))           deser_f32v3(out.scale.floats, val);
        else if(ser_cstring_eq(key, "resolution"))      deser_f32v3(out.resolution.floats, val);
        else if(ser_cstring_eq(key, "filter_minify"))   deser_map_scale_filter(&out.filter_minify, val);
        else if(ser_cstring_eq(key, "filter_magnify"))  deser_map_scale_filter(&out.filter_magnify, val);
        else if(ser_cstring_eq(key, "repeat_u"))        deser_map_repeat(&out.repeat_u, val); //log here
        else if(ser_cstring_eq(key, "repeat_v"))        deser_map_repeat(&out.repeat_v, val);
        else if(ser_cstring_eq(key, "repeat_w"))        deser_map_repeat(&out.repeat_w, val);
        else if(ser_cstring_eq(key, "gamma"))           deser_f32(&out.gamma, val);
        else if(ser_cstring_eq(key, "brightness"))      deser_f32(&out.brightness, val);
        else if(ser_cstring_eq(key, "contrast"))        deser_f32(&out.contrast, val);
        else if(ser_cstring_eq(key, "channels_count"))  deser_i32(&out.channels_count, val);
        else if(ser_cstring_eq(key, "channels_idices1"))
        {
            if(val.type == SER_LIST_BEGIN)
            {
                int index = 0;
                for(Ser_Value channel_index1 = {0}; deser_iterate_list(val, &channel_index1); )
                    index += deser_i32(&out.channels_idices1[index], channel_index1); //log here
            }
        }
    }

    *out_map_info = out;
    return true;
}


void ser_map_repeat(Ser_Writer* context, Map_Repeat repeat)
{
    switch(repeat)
    {
        case MAP_REPEAT_REPEAT:             ser_cstring(context, "repeat"); break;
        case MAP_REPEAT_MIRRORED_REPEAT:    ser_cstring(context, "mirrored"); break;
        case MAP_REPEAT_CLAMP_TO_EDGE:      ser_cstring(context, "clamp_to_edge"); break;
        case MAP_REPEAT_CLAMP_TO_BORDER:    ser_cstring(context, "clamp_to_border"); break;
        default:                            ser_cstring(context, "invalid"); break;
    }
}

bool ser_map_scale_filter(Ser_Writer* context, Map_Scale_Filter filter)
{
    switch(filter)
    {
        case MAP_SCALE_FILTER_BILINEAR:     ser_cstring(context, "bilinear"); break;
        case MAP_SCALE_FILTER_TRILINEAR:    ser_cstring(context, "trilinear"); break;
        case MAP_SCALE_FILTER_NEAREST:      ser_cstring(context, "nearest"); break; 
        default:                            ser_cstring(context, "invalid"); break; 
    }
}

bool ser_map_info(Ser_Writer* context, Map_Info info)
{
    ser_recovery_object_begin(context, "Map_Info:Magic", -1);
    ser_cstring(context, "offset");          ser_f32v3(context, info.offset.floats);
    ser_cstring(context, "scale");           ser_f32v3(context, info.scale.floats);
    ser_cstring(context, "resolution");      ser_f32v3(context, info.resolution.floats);
    ser_cstring(context, "filter_minify");   ser_map_scale_filter(context, info.filter_minify);
    ser_cstring(context, "filter_magnify");  ser_map_scale_filter(context, info.filter_magnify);
    ser_cstring(context, "repeat_u");        ser_map_repeat(context, info.repeat_u);
    ser_cstring(context, "repeat_v");        ser_map_repeat(context, info.repeat_v);
    ser_cstring(context, "repeat_w");        ser_map_repeat(context, info.repeat_w);
    ser_cstring(context, "gamma");           ser_f32(context, info.gamma);
    ser_cstring(context, "brightness");      ser_f32(context, info.brightness);
    ser_cstring(context, "contrast");        ser_f32(context, info.contrast);
    ser_cstring(context, "channels_count");  ser_i32(context, info.channels_count);
    ser_cstring(context, "channels_idices1");
    ser_list_begin(context);
    for(int i = 0; i < MAX_CHANNELS; i++)
        ser_i32(context, info.channels_idices1[i]);
    ser_list_end(context);
    ser_recovery_object_end(context, "Map_Info:Magic", -1);
}

