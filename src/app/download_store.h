#ifndef LUMEN_DOWNLOAD_STORE_H
#define LUMEN_DOWNLOAD_STORE_H
/* installs the platform download location and loads the finished-downloads list from data_dir */
void download_store_init(const char *data_dir);

/* The Downloads page's file browser: the folder downloads go to, the highest folder it may go up
   to, whether every file is visible (Android lists only Lumen's own files without "All files
   access"), asking for that access, and opening a file in the app the system picks. */
#include <stdbool.h>
const char *download_store_dir(void);
const char *download_store_top(void);
bool download_store_all_files(void);
void download_store_request_all_files(void);
void download_store_open_path(const char *path);
#include <stddef.h>
/* the Downloads viewer: page (from 0) of a PDF rendered width pixels wide into an image file; its page
   count, the image's path and size. Android only; false elsewhere or when the PDF cannot be read. */
bool download_store_pdf_page(const char *path, int page, int width, int *pages, char *image, size_t image_n, int *w, int *h);
#endif
