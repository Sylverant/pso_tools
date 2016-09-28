/*
    Sylverant PSO Tools
    PSO Archive Tool
    Copyright (C) 2016 Lawrence Sebald

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

#ifndef WINDOWS_COMPAT_H

#include <time.h>

int mkstemp(char *tmpl);
char *basename(char *input);
int my_rename(const char *old, const char *new);
int utimes(const char *path, const struct timeval tv[2]);

#endif /* !WINDOWS_COMPAT_H */
