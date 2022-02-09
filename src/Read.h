#pragma once

struct Image {
    unsigned long sizeX;
    unsigned long sizeY;
    char* data;
};

//https://stackoverflow.com/a/42043320/15291447
inline int ImageLoad(char const* filename, Image* image) {
    FILE* file;

    auto result = [&file, filename, image]() -> int {
        unsigned long size; // size of the image in bytes.
        unsigned long i; // standard counter.
        unsigned short int planes; // number of planes in image (must be 1)
        unsigned short int bpp; // number of bits per pixel (must be 24)
        char temp; // temporary color storage for bgr-rgb conversion.
        // make sure the file is there.
		errno_t err;
        if ( (err = fopen_s(&file, filename, "rb")) ) {
            printf("File Not Found %s:%s\n", filename, strerror(err));
            return 0;
        }
        // seek through the bmp header, up to the width/height:
        fseek(file, 18, SEEK_CUR);
        // read the width
        if ((i = fread(&image->sizeX, 4, 1, file)) != 1) {
            printf("Error reading width from %s.\n", filename);
            return 0;
        }
        //printf("Width of %s: %lu\n", filename, image->sizeX);
        // read the height
        if ((i = fread(&image->sizeY, 4, 1, file)) != 1) {
            printf("Error reading height from %s.\n", filename);
            return 0;
        }
        //printf("Height of %s: %lu\n", filename, image->sizeY);
        // calculate the size (assuming 24 bits or 3 bytes per pixel).
        size = image->sizeX * image->sizeY * 3;
        // read the planes
        if ((fread(&planes, 2, 1, file)) != 1) {
            printf("Error reading planes from %s.\n", filename);
            return 0;
        }
        if (planes != 1) {
            printf("Planes from %s is not 1: %u\n", filename, planes);
            return 0;
        }
        // read the bitsperpixel
        if ((i = fread(&bpp, 2, 1, file)) != 1) {
            printf("Error reading bpp from %s.\n", filename);
            return 0;
        }
        if (bpp != 24) {
            printf("Bpp from %s is not 24: %u\n", filename, bpp);
            return 0;
        }
        // seek past the rest of the bitmap header.
        fseek(file, 24, SEEK_CUR);
        // read the data.
        image->data = (char*)malloc(size);
        if (image->data == NULL) {
            printf("Error allocating memory for color-corrected image data");
            return 0;
        }
        if ((i = fread(image->data, size, 1, file)) != 1) {
            printf("Error reading image data from %s.\n", filename);
            return 0;
        }
        for (i = 0; i+2 < size; i += 3) { // reverse all of the colors. (bgr -> rgb)
            temp = image->data[i];
            image->data[i] = image->data[i + 2];
            image->data[i + 2] = temp;
        }
        // we're done.
        return 1;
    }();

    fclose(file);
    return result;
}

template<GLsizei count>
inline void loadGLImages(GLuint (&textures)[count], char const* (&paths)[count])
{
    glGenTextures(count, &textures[0]);
    for (GLsizei i = 0; i < count; ++i) {
        Image image;
        ImageLoad(paths[i], &image);
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Create Texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.sizeX, image.sizeY, 0,
            GL_RGB, GL_UNSIGNED_BYTE, image.data);
        glBindTexture(GL_TEXTURE_2D, 0);


        //delete[] image.data;
    }
}
