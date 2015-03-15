/*
    This file is part of Sylverant PSO Server.

    Copyright (C) 2014 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SYLVERANT__PRS_H
#define SYLVERANT__PRS_H

#include <stddef.h>
#include <stdint.h>

/* Compress a buffer with PRS compression.

   This function compresses the data in the src buffer into a new buffer. This
   function will never produce output larger than that of the prs_archive
   function, and will usually beat that function rather significantly.

   In testing, the compressed output of this function actually beats Sega's own
   compression slightly (by 100 bytes or so on an uncompressed version of the
   ItemPMT.prs from PSO Episode I & II Plus).

   It is the caller's responsibility to free *dst when it is no longer in use.

   Returns a negative value on failure (specifically something from <errno.h>.
   Returns the size of the compressed output on success.
*/
extern int prs_compress(const uint8_t *src, uint8_t **dst, size_t src_len);

/* Archive a buffer in PRS format.

   This function archives the data in the src buffer into a new buffer. This
   function will always produce output that is larger in size than the input
   data (it does not actually compress the output. There's probably no good
   reason to ever use this, but it is here if you want it for some reason.

   All the notes about parameters and return values from prs_compress also apply
   to this function. The size of the output from this function will be equal to
   the return value of prs_max_compressed_size when called on the same length.
*/
extern int prs_archive(const uint8_t *src, uint8_t **dst, size_t src_len);

/* Return the maximum size of archiving a buffer in PRS format.

   This function returns the size that prs_archive will spit out. This is used
   internally to allocate memory for prs_archive and prs_compress and probably
   has little utility outside of that.
*/
extern size_t prs_max_compressed_size(size_t len);

/* Decompress a PRS archive from a file.

   This function opens the file specified and decompresses the data from the
   file into a newly allocated memory buffer.

   It is the caller's responsibility to free *dst when it is no longer in use.

   Returns a negative value on failure (specifically something from <errno.h>).
   Returns the size of the decompressed output on success.
*/
extern int prs_decompress_file(const char *fn, uint8_t **dst);

/* Decompress PRS-compressed data from a memory buffer.

   This function decompresses PRS-compressed data from the src buffer into a
   newly allocated memory buffer.

   It is the caller's responsibility to free *dst when it is no longer in use.

   Returns a negative value on failure (specifically something from <errno.h>).
   Returns the size of the decompressed output on success.
*/
extern int prs_decompress_buf(const uint8_t *src, uint8_t **dst,
                              size_t src_len);

/* Decompress PRS-compressed data from a memory buffer into a previously
   allocated memory buffer.

   This function decompresses PRS-compressed data from the src buffer into the
   previously allocated allocated memory buffer dst. You must have already
   allocated the buffer at dst, and it should be at least the size returned by
   prs_decompress_size on the compressed input (otherwise, you will get an error
   back from the function).

   Returns a negative value on failure (specifically something from <errno.h>).
   Returns the size of the decompressed output on success.
*/
extern int prs_decompress_buf2(const uint8_t *src, uint8_t *dst, size_t src_len,
                               size_t dst_len);

/* Determine the size that the PRS-compressed data in a buffer will expand to.

   This function essentially decompresses the PRS-compressed data from the src
   buffer without writing any output. By doing this, it determines the actual
   size of the decompressed output and returns it.

   Returns a negative value on failure (specifically something from <errno.h>).
   Returns the size of the decompressed output on success.
*/
extern int prs_decompress_size(const uint8_t *src, size_t src_len);

#endif /* !SYLVERANT__PRS_H */
