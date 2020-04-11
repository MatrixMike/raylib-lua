/*
  Copyright (C) 2020 Astie Teddy

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
  OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/* Raylua embedded executable */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <raylib.h>

#include "raylua.h"
#include "lib/miniz.h"

#ifndef RAYLUA_NO_BUILDER
int raylua_builder_boot(lua_State *L);
#endif

static mz_zip_archive zip_file;

int raylua_loadfile(lua_State *L)
{
  const char *path = luaL_checkstring(L, 1);

  int index = mz_zip_reader_locate_file(&zip_file, path, NULL, 0);
  if (index == -1) {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: File not found.", path);
    return 2;
  }

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip_file, index, &stat)) {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: Can't get file information.", path);
    return 2;
  }

  size_t size = stat.m_uncomp_size;
  char *buffer = malloc(size);
  if (buffer == NULL) {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: Can't allocate file buffer.", path);
    return 2;
  }

  mz_zip_reader_extract_to_mem(&zip_file, index, buffer, size, 0);

  lua_pushlstring(L, buffer, size);
  free(buffer);
  return 1;
}

int raylua_listfiles(lua_State *L)
{
  size_t count = mz_zip_reader_get_num_files(&zip_file);
  char filename[1024];

  lua_createtable(L, count, 0);

  size_t i = 0;
  while (i < count) {
    mz_zip_reader_get_filename(&zip_file, i, filename, sizeof(filename));
    lua_pushstring(L, filename);

    lua_rawseti(L, -2, i + 1);
    i++;
  }

  return 1;
}

unsigned char *raylua_loadFileData(const char *path, unsigned int *out_size)
{
  int index = mz_zip_reader_locate_file(&zip_file, path, NULL, 0);
  if (index == -1) {
    printf("RAYLUA: WARN: File not found in payload : '%s'", path);
    return NULL;
  }

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip_file, index, &stat)) {
    printf("RAYLUA: WARN: Can't get file information of '%s' in payload.", path);
    return NULL;
  }

  size_t size = stat.m_uncomp_size;
  unsigned char *buffer = RL_MALLOC(size);
  if (buffer == NULL) {
    printf("RAYLUA: WARN: Can't allocate file buffer for '%s'.", path);
    return NULL;
  }

  mz_zip_reader_extract_to_mem(&zip_file, index, buffer, size, 0);

  *out_size = size;
  return buffer;
}

char *raylua_loadFileText(const char *path)
{
  int index = mz_zip_reader_locate_file(&zip_file, path, NULL, 0);
  if (index == -1) {
    printf("RAYLUA: WARN: File not found in payload : '%s'", path);
    return NULL;
  }

  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip_file, index, &stat)) {
    printf("RAYLUA: WARN: Can't get file information of '%s' in payload.", path);
    return NULL;
  }

  size_t size = stat.m_uncomp_size;
  char *buffer = RL_MALLOC(size + 1);
  if (buffer == NULL) {
    printf("RAYLUA: WARN: Can't allocate file buffer for '%s'.", path);
    return NULL;
  }

  buffer[size] = '\0';

  mz_zip_reader_extract_to_mem(&zip_file, index, buffer, size, 0);
  return buffer;
}

static bool raylua_init_payload(const char *path)
{
  mz_zip_zero_struct(&zip_file);

  return mz_zip_reader_init_file(&zip_file, path, 0);
}

int main(int argc, const char **argv)
{
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  if (L == NULL)
    puts("RAYLUA: Unable to initialize Lua.");

  /* Populate arg. */
  lua_newtable(L);

  int i = 0;
  while (argc != i) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i);

    i++;
  }

  lua_setglobal(L, "arg");

  const char *path = argv[0];
  #ifdef WIN32
  /* Executable name translation. */
  size_t path_len = strlen(path);
  char new_path[path_len + 5];

  if (path_len > 4 && stricmp(path + path_len - 4, ".exe")) {
    strcpy(new_path, path);
    strcpy(new_path + path_len, ".exe");

    printf("RAYLUA: Translated self executable name from %s to %s.\n", path, new_path);
    path = new_path;
  }
  #endif

  SetFilesystemOverride((FilesystemOverride){
    .loadFileData = &raylua_loadFileData,
    .loadFileText = &raylua_loadFileText,
  });

  if (!raylua_init_payload(path)) {
    #ifdef RAYLUA_NO_BUILDER
    puts("RAYLUA: No payload.");
    #else
    puts("RAYLUA: No payload, use internal builder.");
    raylua_builder_boot(L);
    #endif
  } else {
    /* Boot on payload. */
    raylua_boot(L, raylua_loadfile, raylua_listfiles, false);
  }

  lua_close(L);
  return 0;
}
