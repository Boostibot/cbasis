#ifndef MODULE_IMAGE
#define MODULE_IMAGE

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef MODULE_ALL_COUPLED
    #include "assert.h"
    #include "allocator.h"
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

//some of the predefined pixel formats.
//Other custom formats can specified by using some positive number for Pixel_Type.
//That number is then the byte size of the data type. 
typedef enum Pixel_Type {
    PIXEL_TYPE_NONE = 0,

    PIXEL_TYPE_U8  = -1,
    PIXEL_TYPE_U16 = -2,
    PIXEL_TYPE_U24 = -3,
    PIXEL_TYPE_U32 = -4,
    PIXEL_TYPE_U64 = -8,

    PIXEL_TYPE_I8  = -11,
    PIXEL_TYPE_I16 = -12,
    PIXEL_TYPE_I24 = -13,
    PIXEL_TYPE_I32 = -14,
    PIXEL_TYPE_I64 = -18,
    
    PIXEL_TYPE_F8  = -21,
    PIXEL_TYPE_F16 = -22,
    PIXEL_TYPE_F32 = -24,
    PIXEL_TYPE_F64 = -28,

    //Any negative number not occupied by previous declarations
    // is considered invalid. This one is just a predefined constant 
    // guaranteed to remain invalid in the future.
    PIXEL_TYPE_INVALID = INT32_MIN
} Pixel_Type;

// A storage of 2D array of pixels holding the bare minimum to be usable. 
// Each pixel is pixel_size bytes long and there are width * height pixels.
// The type can be one of the Pixel_Type enum of negative values
// or it can be positive number of bytes per channel of the pixel.
// The number of channels can be calculated from pixel_size and type
// but is only secondary since most of the time we treat all channels 
// of a pixel as a unit.
typedef struct Image {
    Allocator* allocator;
    uint8_t* pixels; 
    int32_t pixel_size;
    Pixel_Type type; 

    int32_t width;
    int32_t height;

    isize capacity;
} Image;

// A non owning view into a subset of Image's data. 
// Has the same relationship to Image as String to String_Builder
typedef struct Subimage {
    uint8_t* pixels;
    int32_t pixel_size;
    Pixel_Type type;

    int32_t containing_width;
    int32_t containing_height;

    int32_t from_x;
    int32_t from_y;
    
    int32_t width;
    int32_t height;
} Subimage;

#define IMAGE_ALIGN 64 //for simd

//returns the human readable name of the pixel type. 
// Example return values are "uint8_t", "f32", "i64", ..., "custom" (for pixel_type > 0) and "invalid" (pixel_type < 0 and none of the predefined)
EXTERNAL const char* pixel_type_name(Pixel_Type pixel_type);
//Returns the size of the pixel type. The return value is always bigger than 0.
EXTERNAL int32_t pixel_type_size(Pixel_Type pixel_type);
EXTERNAL int32_t pixel_type_size_or_zero(Pixel_Type pixel_type);
EXTERNAL int32_t pixel_channel_count(Pixel_Type pixel_type, isize pixel_size);

EXTERNAL void image_init_unshaped(Image* image, Allocator* alloc);
EXTERNAL void image_init(Image* image, Allocator* alloc, isize pixel_size, Pixel_Type type);
//Initializes image with the given shape. If dat_or_null is not NULL fills it with data, otherwise fills it with 0.
EXTERNAL void image_init_sized(Image* image, Allocator* alloc, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null);
EXTERNAL void image_deinit(Image* image);
EXTERNAL void image_resize(Image* image, isize width, isize height);
EXTERNAL void image_reserve(Image* image, isize capacity);
EXTERNAL void image_copy(Image* to_image, Subimage from_image, isize offset_x, isize offset_y);
EXTERNAL void image_assign(Image* to_image, Subimage from_image);

//Gives the image the specified shape with width, height, channel_count and type. If the new shape is too big reallocates.
//Does not change the content within the image itself, likewise doesnt fill new size with 0 on size increase.
//If data_or_null is not NULL also copies the data into image.
EXTERNAL void image_reshape(Image* image, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null);

EXTERNAL void* image_at(Image image, isize x, isize y);
EXTERNAL Image image_from_image(Image to_copy, Allocator* alloc);
EXTERNAL Image image_from_subimage(Subimage to_copy, Allocator* alloc);

EXTERNAL Subimage image_portion(Image image, isize from_x, isize from_y, isize width, isize height);
EXTERNAL Subimage image_range(Image image, isize from_x, isize from_y, isize to_x, isize to_y);

EXTERNAL int32_t image_channel_count(Image image);
EXTERNAL isize image_pixel_count(Image image);
EXTERNAL isize image_byte_stride(Image image);
EXTERNAL isize image_byte_size(Image image);

EXTERNAL Subimage subimage_of(Image image);
EXTERNAL Subimage subimage_make(void* pixels, isize width, isize height, isize pixel_size, Pixel_Type type);
EXTERNAL bool subimage_is_contiguous(Subimage view); //returns true if the subimage is contiguous in memory
EXTERNAL bool subimage_is_overlapping(Subimage a, Subimage b); //returns true if the subimages overlap in memory
EXTERNAL bool subimage_is_same_format(Subimage a, Subimage b); //returns true if the subimages have the same format.
EXTERNAL void* subimage_at(Subimage image, isize x, isize y);
EXTERNAL int32_t subimage_channel_count(Subimage image);
EXTERNAL isize subimage_pixel_count(Subimage image);
EXTERNAL isize subimage_byte_stride(Subimage image);
EXTERNAL isize subimage_byte_size(Subimage image);

EXTERNAL Subimage subimage_portion(Subimage image, isize from_x, isize from_y, isize width, isize height);
EXTERNAL Subimage subimage_range(Subimage image, isize from_x, isize from_y, isize to_x, isize to_y);

//copies to from_image into to_image at the specified offset. The images need to have the same format and the same number of channels but can overlap
EXTERNAL void subimage_copy(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y); 
//Copies to from_image into to_image at the specified offset. The images need to have the same format but can have different number of channels and must not overlap.
//If to_image has more channels than from_image and pad_with_or_minus_one != -1 then memsets each row to byte value of pad_with_or_minus_one.
//For more complicated patterns use memtile then call this function with pad_with_or_minus_one == -1.
EXTERNAL void subimage_convert(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y, int pad_with_or_minus_one);  

EXTERNAL void subimage_flip_x_inplace(Subimage image, void* temp_pixel, isize temp_size); //flips the image in place. temp_pixel needs to point to at least image.pixel_size bytes.
EXTERNAL void subimage_flip_y_inplace(Subimage image, void* temp_row, isize temp_size); //flips the image in place. temp_pixel needs to point to at least image.pixel_size bytes.

EXTERNAL void subimage_flip_x(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y);
EXTERNAL void subimage_flip_y(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y);

#endif

#define MODULE_IMPL_ALL

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_IMAGE)) && !defined(MODULE_HAS_IMPL_IMAGE)
#define MODULE_HAS_IMPL_IMAGE

#ifndef ASSERT
    #include <assert.h>
    #include <stdlib.h>
    #include <stdio.h>
    #define ASSERT(x, ...) assert(x)
    #define REQUIRE(x, ...) assert(x)
    #define CHECK_BOUNDS(i, count, ...) assert(0 <= (i) && (i) <= (count))
    #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
#endif

EXTERNAL const char* pixel_type_name(Pixel_Type pixel_type)
{
    switch(pixel_type)
    {
        case PIXEL_TYPE_NONE: return "none";
        case PIXEL_TYPE_U8: return "uint8_t";
        case PIXEL_TYPE_U16: return "u16";
        case PIXEL_TYPE_U24: return "u24";
        case PIXEL_TYPE_U32: return "u32";
        case PIXEL_TYPE_U64: return "u64";
        
        case PIXEL_TYPE_I8: return "i8";
        case PIXEL_TYPE_I16: return "i16";
        case PIXEL_TYPE_I24: return "i24";
        case PIXEL_TYPE_I32: return "int32_t";
        case PIXEL_TYPE_I64: return "i64";

        case PIXEL_TYPE_F8: return "f8";
        case PIXEL_TYPE_F16: return "f16";
        case PIXEL_TYPE_F32: return "f32";
        case PIXEL_TYPE_F64: return "f64";

        case PIXEL_TYPE_INVALID: 
        default: 
        {
            if(pixel_type > 0)
                return "custom";
            else
                return "invalid";
        }
    }
}

EXTERNAL int32_t pixel_type_size_or_zero(Pixel_Type pixel_type)
{
    switch(pixel_type)
    {
        case PIXEL_TYPE_NONE:  return 0;
        case PIXEL_TYPE_U8:  return 1;
        case PIXEL_TYPE_U16: return 2;
        case PIXEL_TYPE_U24: return 3;
        case PIXEL_TYPE_U32: return 4;
        case PIXEL_TYPE_U64: return 8;
        
        case PIXEL_TYPE_I8:  return 1;
        case PIXEL_TYPE_I16: return 2;
        case PIXEL_TYPE_I24: return 3;
        case PIXEL_TYPE_I32: return 4;
        case PIXEL_TYPE_I64: return 8;

        case PIXEL_TYPE_F8:  return 1;
        case PIXEL_TYPE_F16: return 2;
        case PIXEL_TYPE_F32: return 4;
        case PIXEL_TYPE_F64: return 8;
        
        case PIXEL_TYPE_INVALID: 
        default: {
            if(pixel_type > 0)
                return (int32_t) pixel_type;
            else
                return 0;
        }
    }
}

EXTERNAL int32_t pixel_type_size(Pixel_Type pixel_type)
{
    int32_t size = pixel_type_size_or_zero(pixel_type);
    return size > 1 ? size : 1; 
}

EXTERNAL int32_t pixel_channel_count(Pixel_Type pixel_type, isize pixel_size)
{
    int32_t format_size = pixel_type_size(pixel_type);
    ASSERT(format_size > 0);
    int32_t out = (int32_t) pixel_size / format_size;
    return out;
}

EXTERNAL int32_t image_channel_count(Image image)
{
    return pixel_channel_count(image.type, image.pixel_size);
}

EXTERNAL isize image_pixel_count(Image image)
{
    return image.width * image.height;
}

EXTERNAL isize image_byte_stride(Image image)
{
    isize byte_stride = image.pixel_size * image.width;
    return byte_stride;
}

EXTERNAL isize image_byte_size(Image image)
{
    isize pixel_count = image_pixel_count(image);
    return image.pixel_size * pixel_count;
}

EXTERNAL void image_deinit(Image* image)
{
    if(image->capacity)
        (*image->allocator)(image->allocator, 0, 0, image->pixels, image->capacity, IMAGE_ALIGN, NULL);
    memset(image, 0, sizeof *image);
}

EXTERNAL void image_init(Image* image, Allocator* alloc, isize pixel_size, Pixel_Type type)
{
    image_deinit(image);
    image->allocator = alloc;
    image->pixel_size = (int32_t) pixel_size;
    image->type = type;
}

EXTERNAL void image_init_unshaped(Image* image, Allocator* alloc)
{
    image_deinit(image);
    image->allocator = alloc;
}

EXTERNAL void image_init_sized(Image* image, Allocator* alloc, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null)
{
    image_deinit(image);
    image->allocator = alloc;
    image_reshape(image, width, height, pixel_size, type, data_or_null);
    if(data_or_null == NULL)
        memset(image->pixels, 0, (size_t) image->capacity);
}

EXTERNAL void* image_at(Image image, isize x, isize y)
{
    CHECK_BOUNDS(x, image.width);
    CHECK_BOUNDS(y, image.height);

    isize byte_stride = image_byte_stride(image);
    uint8_t* pixel = image.pixels + x*image.pixel_size + y*byte_stride;

    return pixel;
}

EXTERNAL int32_t subimage_channel_count(Subimage image)
{
    return pixel_channel_count(image.type, image.pixel_size);
}

EXTERNAL isize subimage_byte_stride(Subimage image)
{
    return image.containing_width * image.pixel_size;
}

EXTERNAL isize subimage_pixel_count(Subimage image)
{
    return image.width * image.height;
}

EXTERNAL isize subimage_byte_size(Subimage image)
{
    isize pixel_count = subimage_pixel_count(image);
    return image.pixel_size * pixel_count;
}

EXTERNAL Subimage subimage_make(void* pixels, isize width, isize height, isize pixel_size, Pixel_Type type)
{
    Subimage view = {0};
    view.pixels = (uint8_t*) pixels;
    view.pixel_size = (int32_t) pixel_size;
    view.type = type;

    view.containing_width = (int32_t) width;
    view.containing_height = (int32_t) height;

    view.from_x = 0;
    view.from_y = 0;
    view.width = (int32_t) width;
    view.height = (int32_t) height;
    return view;
}

EXTERNAL Subimage subimage_of(Image image)
{
    return subimage_make(image.pixels, image.width, image.height, image.pixel_size, image.type);
}

EXTERNAL bool subimage_is_contiguous(Subimage view)
{
    return (view.from_x == 0 && view.width == view.containing_width) || view.height <= 1;
}

EXTERNAL bool subimage_is_overlapping(Subimage a, Subimage b)
{
    if(a.pixels != b.pixels)
        return false;
        
    isize dx = b.from_x - a.from_x;
    isize dy = b.from_y - a.from_y;

    isize ah = a.height;
    isize bh = b.height;
    
    if(-bh >= dy || dy >= ah)
        return false;

    isize aw = a.width*a.pixel_size;
    isize bw = b.width*b.pixel_size;
    return -bw < dx && dx < aw;
}

EXTERNAL Subimage subimage_range(Subimage view, isize from_x, isize from_y, isize to_x, isize to_y)
{
    Subimage out = view;
    CHECK_BOUNDS(from_x, out.width + 1);
    CHECK_BOUNDS(from_y, out.height + 1);
    CHECK_BOUNDS(to_x, out.width + 1);
    CHECK_BOUNDS(to_y, out.height + 1);

    CHECK_BOUNDS(from_x, to_x);
    CHECK_BOUNDS(from_y, to_y);

    out.from_x = (int32_t) from_x;
    out.from_y = (int32_t) from_y;
    out.width = (int32_t) (to_x - from_x);
    out.height = (int32_t) (to_y - from_y);

    return out;
}

EXTERNAL Subimage subimage_portion(Subimage view, isize from_x, isize from_y, isize width, isize height)
{
    return subimage_range(view, from_x, from_y, from_x + width, from_y + height);
}

EXTERNAL Subimage image_portion(Image image, isize from_x, isize from_y, isize width, isize height)
{   
    return subimage_range(subimage_of(image), from_x, from_y, from_x + width, from_y + height);
}
EXTERNAL Subimage image_range(Image image, isize from_x, isize from_y, isize to_x, isize to_y)
{
    return subimage_range(subimage_of(image), from_x, from_y, to_x, to_y);
}

EXTERNAL void* subimage_at(Subimage subimage, isize x, isize y)
{
    CHECK_BOUNDS(x, subimage.width);
    CHECK_BOUNDS(y, subimage.height);

    int32_t containing_x = (int32_t) x + subimage.from_x;
    int32_t containing_y = (int32_t) y + subimage.from_y;
    
    isize byte_stride = subimage.containing_width * subimage.pixel_size;;

    uint8_t* data = (uint8_t*) subimage.pixels;
    isize offset = containing_x*subimage.pixel_size + containing_y*byte_stride;
    uint8_t* pixel = data + offset;

    return pixel;
}

EXTERNAL void subimage_copy(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    if(from_image.width == 0 || from_image.height == 0)
        return;
        
    REQUIRE(from_image.type == to_image.type && from_image.pixel_size == to_image.pixel_size);
    isize to_image_stride = subimage_byte_stride(to_image); 
    isize from_image_stride = subimage_byte_stride(from_image); 
    isize row_byte_size = from_image.width * from_image.pixel_size;

    Subimage to_portion = subimage_portion(to_image, offset_x, offset_y, from_image.width, from_image.height);
    uint8_t* to_image_ptr = (uint8_t*) subimage_at(to_portion, 0, 0);
    uint8_t* from_image_ptr = (uint8_t*) subimage_at(from_image, 0, 0);

    //if both are contiguous (full width) then we can do just a single move
    if(subimage_is_contiguous(to_portion) && subimage_is_contiguous(from_image))
        memmove(to_image_ptr, from_image_ptr, (size_t) row_byte_size*to_portion.width);
    //Copy in the right order so we dont override any data
    else {
        if(from_image_ptr >= to_image_ptr)
        {
            for(isize y = 0; y < from_image.height; y++) { 
                memmove(to_image_ptr, from_image_ptr, (size_t) row_byte_size);
                to_image_ptr += to_image_stride;
                from_image_ptr += from_image_stride;
            }
        }
        else
        {
            //Reverse order copy
            to_image_ptr += from_image.height*to_image_stride;
            from_image_ptr += from_image.height*from_image_stride;

            for(isize y = 0; y < from_image.height; y++) { 
                to_image_ptr -= to_image_stride;
                from_image_ptr -= from_image_stride;
                memmove(to_image_ptr, from_image_ptr, (size_t) row_byte_size);
            }
        } 
    }
}


EXTERNAL Image image_from_subimage(Subimage view, Allocator* alloc)
{
    Image image = {0};
    image_init_unshaped(&image, alloc);
    image_reshape(&image, view.width, view.height, view.pixel_size, view.type, NULL);

    image_copy(&image, view, 0, 0);
    return image;
}

EXTERNAL Image image_from_image(Image to_copy, Allocator* alloc)
{
    Image image = {0};
    image.allocator = alloc;
    image_assign(&image, subimage_of(to_copy));
    return image;
}

EXTERNAL void image_reserve(Image* image, isize capacity)
{
    REQUIRE(image != NULL);
    if(capacity > image->capacity)
    {
        REQUIRE(image->allocator != NULL);

        //this weird realloc is on purpose to allow copying from self to self.
        uint8_t* new_pixels = (uint8_t*) (*image->allocator)(image->allocator, 0, capacity, NULL, 0, IMAGE_ALIGN, NULL);
        isize old_byte_size = image_byte_size(*image);
        
        memcpy(new_pixels, image->pixels, (size_t) old_byte_size);
        if(image->capacity)
            (*image->allocator)(image->allocator, 0, 0, image->pixels, image->capacity, IMAGE_ALIGN, NULL);

        image->pixels = new_pixels;
        image->capacity = capacity;
    }
}

EXTERNAL void image_reshape(Image* image, isize width, isize height, isize pixel_size, Pixel_Type type, const void* data_or_null)
{
    REQUIRE(image != NULL && width >= 0 && height >= 0);
    isize needed_size = width*height*pixel_size;
    if(needed_size > image->capacity)
    {
        REQUIRE(image->allocator != NULL);

        uint8_t* new_pixels = (uint8_t*) (*image->allocator)(image->allocator, 0, needed_size, NULL, 0, IMAGE_ALIGN, NULL);
        if(image->capacity)
            (*image->allocator)(image->allocator, 0, 0, image->pixels, image->capacity, IMAGE_ALIGN, NULL);

        image->pixels = new_pixels;
        image->capacity = needed_size;
    }

    if(data_or_null)
    {
        ASSERT(image->capacity >= needed_size);   
        memmove(image->pixels, data_or_null, (size_t) needed_size);
    }

    image->width = (int32_t) width;
    image->height = (int32_t) height;
    image->pixel_size = (int32_t) pixel_size;
    image->type = type;
}

EXTERNAL void image_assign(Image* to_image, Subimage from_image)
{
    image_reshape(to_image, from_image.width, from_image.height, from_image.pixel_size, from_image.type, from_image.pixels);
    image_copy(to_image, from_image, 0, 0);
}

EXTERNAL void image_resize(Image* image, isize width, isize height)
{
    REQUIRE(image != NULL && width >= 0 && height >= 0);
    
    if(image->width == width && image->height == height)
        return;

    ASSERT(image->allocator != NULL);
    if(image->pixel_size == 0)
    {
        ASSERT(image->width == 0 && image->height == 0);
        image->pixel_size = pixel_type_size(image->type);
    }

    isize new_byte_size = width * height * (isize) image->pixel_size;

    Image new_image = *image;
    new_image.width = (int32_t) width;
    new_image.height = (int32_t) height;
    if(new_byte_size > image->capacity)
    {
        new_image.pixels = (uint8_t*) (*image->allocator)(image->allocator, 0, new_byte_size, NULL, 0, IMAGE_ALIGN, NULL);
        new_image.capacity = new_byte_size;
        memset(new_image.pixels, 0, (size_t) new_byte_size);
    }

    Subimage to_view = subimage_of(new_image);
    Subimage from_view = subimage_of(*image);
    from_view.width = from_view.width < to_view.width ? from_view.width : to_view.width;
    from_view.height = from_view.height < to_view.height ? from_view.height : to_view.height;

    subimage_copy(to_view, from_view, 0, 0);

    if(new_byte_size > image->capacity)
        (*image->allocator)(image->allocator, 0, 0, image->pixels, image->capacity, IMAGE_ALIGN, NULL);

    *image = new_image;
}

EXTERNAL void image_copy(Image* to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    subimage_copy(subimage_of(*to_image), from_image, offset_x, offset_y);
}

EXTERNAL void subimage_convert(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y, int pad_with_or_minus_one)
{
    if(from_image.width == 0 || from_image.height == 0)
        return;
        
    if(from_image.type == to_image.type && from_image.pixel_size == to_image.pixel_size)
        subimage_copy(to_image, from_image, offset_x, offset_y);
    else {
        REQUIRE(subimage_is_overlapping(to_image, from_image) == false, "must not overlap in memory");
        REQUIRE(from_image.type == to_image.type);

        Subimage to_portion = subimage_portion(to_image, offset_x, offset_y, from_image.width, from_image.height);
        isize min_size = from_image.pixel_size < to_image.pixel_size ? from_image.pixel_size : to_image.pixel_size;
        for(isize y = 0; y < from_image.height; y++)
        {
            uint8_t* to_row = (uint8_t*) subimage_at(to_portion, 0, y);
            uint8_t* from_row = (uint8_t*) subimage_at(from_image, 0, y);

            //pad the space if necessary
            if(pad_with_or_minus_one != -1 && from_image.pixel_size < to_image.pixel_size)
                memset(to_row, pad_with_or_minus_one, to_portion.width*to_portion.pixel_size);

            //perform the inner loop specialized for some common sizes.
            #define SUBIMAGE_LOOP_COPY(size) \
                for(isize x = 0; x < from_image.width; x++) \
                    memcpy(to_row + x*to_portion.pixel_size, from_row + x*from_image.pixel_size, size) \

            switch(min_size) {
                case 1:  SUBIMAGE_LOOP_COPY(1); break;
                case 2:  SUBIMAGE_LOOP_COPY(2); break;
                case 3:  SUBIMAGE_LOOP_COPY(3); break;
                case 4:  SUBIMAGE_LOOP_COPY(4); break;
                case 6:  SUBIMAGE_LOOP_COPY(6); break;
                case 8:  SUBIMAGE_LOOP_COPY(8); break;
                case 12: SUBIMAGE_LOOP_COPY(12); break;
                case 16: SUBIMAGE_LOOP_COPY(16); break;
                case 32: SUBIMAGE_LOOP_COPY(32); break;
                default: SUBIMAGE_LOOP_COPY(min_size); break;
            }

            #undef SUBIMAGE_LOOP_COPY
        }
    }
}

EXTERNAL void subimage_flip_y_inplace(Subimage image, void* temp_row, isize temp_size)
{
    isize row_size = subimage_byte_stride(image);
    REQUIRE(temp_size >= row_size);
    for(isize y = 0; y < image.height/2; y++) {
        void* from_row1 = subimage_at(image, 0, y);
        void* from_row2 = subimage_at(image, 0, image.height - y - 1);
        
        memcpy(temp_row, from_row1, row_size);
        memcpy(from_row1, from_row2, row_size);
        memcpy(from_row2, temp_row, row_size);
    }
}

EXTERNAL void subimage_flip_x_inplace(Subimage image, void* temp_pixel, isize temp_size)
{
    REQUIRE(image.pixel_size <= temp_size); 
    isize stride = subimage_byte_stride(image);   
    uint8_t* image_ptr = (uint8_t*) subimage_at(image, 0, 0);
    for(isize y = 0; y < image.height; y++) 
    {
        uint8_t* row = image_ptr + stride*y;
        void* te = temp_pixel;
        #define SUBIMAGE_FLIP_ROW(size)        \
            for(isize x = 0; x < image.width/2; x++) {        \
                uint8_t* a1 = row + size*x;             \
                uint8_t* a2 = row + size*(image.width - x - 1);   \
                memcpy(te, a1, size);                   \
                memcpy(a1, a2, size);                   \
                memcpy(a2, te, size);                   \
            }                                           \

        switch(image.pixel_size) {
            case 1:  SUBIMAGE_FLIP_ROW(1); break;
            case 2:  SUBIMAGE_FLIP_ROW(2); break;
            case 3:  SUBIMAGE_FLIP_ROW(3); break;
            case 4:  SUBIMAGE_FLIP_ROW(4); break;
            case 6:  SUBIMAGE_FLIP_ROW(6); break;
            case 8:  SUBIMAGE_FLIP_ROW(8); break;
            case 12: SUBIMAGE_FLIP_ROW(12); break;
            case 16: SUBIMAGE_FLIP_ROW(16); break;
            case 32: SUBIMAGE_FLIP_ROW(32); break;
            default: SUBIMAGE_FLIP_ROW(image.pixel_size); break;
        }
        #undef SUBIMAGE_FLIP_ROW
    }
}

EXTERNAL void subimage_flip_y(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    REQUIRE(subimage_is_overlapping(to_image, from_image) == false && to_image.pixel_size == from_image.pixel_size);
    Subimage to_portion = subimage_portion(to_image, offset_x, offset_y, from_image.width, from_image.height);
    for(isize y = 0; y < to_portion.height; y++) {
        uint8_t* from_row = (uint8_t*) subimage_at(from_image, 0, y);
        uint8_t* to_row = (uint8_t*) subimage_at(to_portion, 0, to_portion.height - y - 1);
        memcpy(to_row, from_row, to_portion.width*to_portion.pixel_size);
    }
}

EXTERNAL void subimage_flip_x(Subimage to_image, Subimage from_image, isize offset_x, isize offset_y)
{
    REQUIRE(subimage_is_overlapping(to_image, from_image) == false && to_image.pixel_size == from_image.pixel_size);
    Subimage to_portion = subimage_portion(to_image, offset_x, offset_y, from_image.width, from_image.height);
    for(isize y = 0; y < to_portion.height; y++) {
        uint8_t* from_row = (uint8_t*) subimage_at(from_image, 0, y);
        uint8_t* to_row = (uint8_t*) subimage_at(to_portion, 0, y);
        
        #define SUBIMAGE_FLIP_ROW(size)        \
            for(isize x = 0; x < to_portion.width; x++) {        \
                uint8_t* from_pixel = from_row + size*x;             \
                uint8_t* to_pixel = to_row + size*(to_portion.width - x - 1);   \
                memcpy(to_pixel, from_pixel, size);                   \
            }                                           

        switch(to_portion.pixel_size) {
            case 1:  SUBIMAGE_FLIP_ROW(1); break;
            case 2:  SUBIMAGE_FLIP_ROW(2); break;
            case 3:  SUBIMAGE_FLIP_ROW(3); break;
            case 4:  SUBIMAGE_FLIP_ROW(4); break;
            case 6:  SUBIMAGE_FLIP_ROW(6); break;
            case 8:  SUBIMAGE_FLIP_ROW(8); break;
            case 12: SUBIMAGE_FLIP_ROW(12); break;
            case 16: SUBIMAGE_FLIP_ROW(16); break;
            case 32: SUBIMAGE_FLIP_ROW(32); break;
            default: SUBIMAGE_FLIP_ROW(to_portion.pixel_size); break;
        }
        #undef SUBIMAGE_FLIP_ROW
    }
}

#endif