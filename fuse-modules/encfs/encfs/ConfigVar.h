/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004, Valient Gough
 * 
 * This program is free software; you can distribute it and/or modify it under 
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _ConfigVar_incl_
#define _ConfigVar_incl_

#include "LinkedOwner.h"

#include <string>

class ConfigVar : public rel::LinkedOwner
{
    struct ConfigVarData
    {
	std::string buffer;
	int offset;
    } *pd;

public:
    ConfigVar();
    ConfigVar(const std::string &buffer);
    ConfigVar(const ConfigVar &src);
    ~ConfigVar();

    ConfigVar & operator = (const ConfigVar &src);

    // reset read/write offset..
    void resetOffset();

    // read bytes
    int read(unsigned char *buffer, int size) const;

    // write bytes..
    int write(const unsigned char *data, int size);

    int readBER() const;
    int readBER( int defaultValue ) const;
    void writeBER(int value);

    bool readBool( bool defaultValue ) const;

    void writeString(const char *data, int size);

    // return amount of data in var
    int size() const;
    // return data pointer - returns front of data pointer, not the current
    // position.  
    const char *buffer() const; 

    // return current position in data() buffer.
    int at() const;
};

ConfigVar & operator << (ConfigVar &, bool); 
ConfigVar & operator << (ConfigVar &, int); 
ConfigVar & operator << (ConfigVar &, const std::string &str);

const ConfigVar & operator >> (const ConfigVar &, bool &); 
const ConfigVar & operator >> (const ConfigVar &, int &); 
const ConfigVar & operator >> (const ConfigVar &, std::string &str);

#endif

