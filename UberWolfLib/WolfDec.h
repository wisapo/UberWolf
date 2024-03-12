/*
 *  File: WolfDec.h
 *  Copyright (c) 2023 Sinflower
 *
 *  MIT License
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#pragma once

#include <cstdint>
#include <iterator>
#include <string>
#include <tchar.h>
#include <vector>

#include "Types.h"

using DecryptFunction = int (*)(TCHAR*, const TCHAR*, const char*);

struct DecryptMode
{
	DecryptMode(const std::string& name, const DecryptFunction& decFunc, const std::vector<char> key) :
		name(name),
		decFunc(decFunc),
		key(key)
	{
	}

	DecryptMode(const std::string& name, const DecryptFunction& decFunc, const std::string& key) :
		name(name),
		decFunc(decFunc),
		key(key.begin(), key.end())
	{
		this->key.push_back(0x00); // The key needs to end with 0x00 so the parser knows when to stop
	}

	DecryptMode(const std::string& name, const DecryptFunction& decFunc, const std::vector<unsigned char> key) :
		name(name),
		decFunc(decFunc)
	{
		std::copy(key.begin(), key.end(), std::back_inserter(this->key));
	}

	std::string name;
	DecryptFunction decFunc;
	std::vector<char> key;
};

using DecryptModes = std::vector<DecryptMode>;

class WolfDec
{
public:
	inline static const std::string CONFIG_FILE_NAME = "UberWolfConfig.json";

public:
	WolfDec() :
		WolfDec(L"") {}
	WolfDec(const std::wstring& progName, const uint32_t& mode = -1, const bool& isSubProcess = false);
	~WolfDec();

	operator bool() const
	{
		return m_valid;
	}

	bool IsModeSet() const
	{
		return m_mode != -1;
	}

	bool IsValidFile(const tString& filePath) const;

	bool IsAlreadyUnpacked(const tString& filePath) const;

	bool UnpackArchive(const tString& filePath, const bool& override = false);

	void AddKey(const std::string& name, const bool& useOldDxArc, const Key& key);

	void Reset()
	{
		m_mode = -1;
	}

private:
	void loadConfig();
	bool detectMode(const tString& filePath, const bool& override = false);
	bool runProcess(const tString& filePath, const uint32_t& mode, const bool& override = false) const;

private:
	uint32_t m_mode                = -1;
	DecryptModes m_additionalModes = {};
	std::wstring m_progName;
	bool m_isSubProcess = false;
	bool m_valid        = false;
};
