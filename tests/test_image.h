#pragma once

#include "../image.h"
#include "../allocator_debug.h"

INTERNAL void test_image_builder_copy()
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_PRINT);
    {
        Image from_image  = {0};
        image_init(&from_image, debug_alloc.alloc, sizeof(u16), PIXEL_TYPE_U16);
        image_reserve(&from_image, 1000);
        image_resize(&from_image, 4, 4);

        for(u16 x = 0; x < 4; x++)
            for(u16 y = 0; y < 4; y++)
                *(u16*) image_at(from_image, x, y) = (u16) (x + y*4);
    
        u16 pattern[16] = {0};
        for(u16 i = 0; i < 16; i++)
            pattern[i] = i;

        TEST(memcmp(from_image.pixels, pattern, sizeof(pattern)) == 0);

        Image to_image = {0};
        image_init(&to_image, debug_alloc.alloc, sizeof(u16), PIXEL_TYPE_U16);
        image_resize(&to_image, 2, 2);

        Subimage from_imagev = image_portion(from_image, 1, 1, 2, 2);
        Subimage to_imagev = subimage_of(to_image);

        subimage_copy(to_imagev, from_imagev, 0, 0);
        TEST(*(u16*) image_at(to_image, 0, 0) == 5);
        TEST(*(u16*) image_at(to_image, 1, 0) == 6);
        TEST(*(u16*) image_at(to_image, 0, 1) == 9);
        TEST(*(u16*) image_at(to_image, 1, 1) == 10);

        image_resize(&from_image, 2, 2);
        TEST(*(u16*) image_at(from_image, 0, 0) == 0);
        TEST(*(u16*) image_at(from_image, 1, 0) == 1);
        TEST(*(u16*) image_at(from_image, 0, 1) == 4);
        TEST(*(u16*) image_at(from_image, 1, 1) == 5);

        image_deinit(&from_image);
        image_deinit(&to_image);
    }
    debug_allocator_deinit(&debug_alloc);
}

INTERNAL void test_image()
{
    test_image_builder_copy();
}