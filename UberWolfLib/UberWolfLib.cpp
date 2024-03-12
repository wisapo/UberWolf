/*
 *  File: UberWolfLib.cpp
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

#include "UberWolfLib.h"
#include "Localizer.h"
#include "UberLog.h"
#include "Utils.h"
#include "WolfDec.h"
#include "WolfKeyFinder.h"
#include "WolfPro.h"
#include "WolfUtils.h"
#include "resource.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static const tStrings GAME_EXE_NAMES = {
	TEXT("Game.exe"),
	TEXT("GamePro.exe")
};

static const tString DATA_FOLDER_NAME = TEXT("Data");

// TODO: Maybe implement error callbacks or something that notifies the application about errors

// For parent process calls the first argument is expected to be the game executable path
UberWolfLib::UberWolfLib(const tStrings& argv) :
	m_gameExePath(TEXT("")),
	m_dataFolder(TEXT("")),
	m_valid(false)
{
	if (argv.size() < 1)
		throw std::runtime_error("UberWolfLib: Invalid arguments count");

	uint32_t mode     = -1;
	tString path      = TEXT("");
	bool isSubProcess = IsSubProcess();
	bool override     = false;

	if (isSubProcess && argv.size() >= 3)
	{
		for (std::size_t i = 0; i < argv.size(); i++)
		{
			if (argv[i] == TEXT("-m"))
			{
				if (i + 2 >= argv.size())
					throw std::runtime_error("UberWolfLib: -m argument requires two values");

				mode = std::stoi(WStringToString(argv[i + 1]));
				path = argv[i + 2];

				if (argv.size() > i + 3)
					override = (argv[i + 3] == TEXT("-o"));
				break;
			}
		}
	}

	m_wolfDec = WolfDec(argv[0], mode, isSubProcess);

	if (isSubProcess)
	{
		// For subprocess calls the mode was passed as an argument
		// directly call unpack, which in this case will also terminate the process
		m_wolfDec.UnpackArchive(path.c_str(), override);
		ExitProcess(0);
	}
	else if (argv.size() >= 2)
		InitGame(argv[1]);
}

UberWolfLib::UberWolfLib(int argc, char* argv[]) :
	UberWolfLib(argvToList(argc, argv))
{
}

bool UberWolfLib::InitGame(const tString& gameExePath)
{
	m_valid = false;

	if (!fs::exists(gameExePath))
	{
		ERROR_LOG << std::format(TEXT("UberWolfLib: Invalid game executable path: {}"), gameExePath) << std::endl;
		return false;
	}

	m_gameExePath = gameExePath;

	if (!findDataFolder()) return false;

	m_wolfPro = WolfPro(m_dataFolder, m_dataAsFile);

	m_valid = true;
	return m_valid;
}

UWLExitCode UberWolfLib::UnpackData()
{
	if (!m_valid)
		return UWLExitCode::NOT_INITIALIZED;

	for (const auto& dirEntry : fs::directory_iterator(m_dataFolder))
	{
		if (IsWolfExtension(dirEntry.path().extension()))
		{
			tString target  = FS_PATH_TO_TSTRING(dirEntry.path());
			UWLExitCode uec = unpackArchive(target);
			if (uec != UWLExitCode::SUCCESS) return uec;
		}
	}

	return UWLExitCode::SUCCESS;
}

UWLExitCode UberWolfLib::UnpackArchive(const tString& archivePath)
{
	return unpackArchive(archivePath);
}

UWLExitCode UberWolfLib::FindDxArcKey()
{
	if (!m_valid)
		return UWLExitCode::NOT_INITIALIZED;

	INFO_LOG << LOCALIZE("dec_key_search_msg") << std::endl;

	if (findDxArcKeyFile() == UWLExitCode::SUCCESS)
		return UWLExitCode::SUCCESS;

	if (m_config.useInject)
		return findDxArcKeyInject();

	return UWLExitCode::KEY_DETECT_FAILED;
}

UWLExitCode UberWolfLib::FindProtectionKey(std::string& key)
{
	if (!m_valid)
		return UWLExitCode::NOT_INITIALIZED;

	if (!m_wolfPro.IsWolfPro())
		return UWLExitCode::NOT_WOLF_PRO;

	// Check if the protection file exists
	m_wolfPro.RecheckProtFileState();

	if (m_wolfPro.NeedsUnpacking())
	{
		const tString target = FindExistingWolfFile(m_dataFolder + TEXT("/") + m_wolfPro.GetProtKeyArchiveName());
		if (target.empty())
		{
			ERROR_LOG << std::format(TEXT("UberWolfLib: Could not find protection file: {}"), m_wolfPro.GetProtKeyArchiveName()) << std::endl;
			return UWLExitCode::FILE_NOT_FOUND;
		}

		if (unpackArchive(target) != UWLExitCode::SUCCESS) return UWLExitCode::UNPACK_FAILED;
	}

	Key keyVec = m_wolfPro.GetProtectionKey();
	if (keyVec.empty())
		return UWLExitCode::PROT_KEY_DETECT_FAILED;

	key = "";
	for (const uint8_t& byte : keyVec)
		key += static_cast<char>(byte);

	m_wolfPro.RemoveProtection();

	return UWLExitCode::SUCCESS;
}

UWLExitCode UberWolfLib::FindProtectionKey(std::wstring& key)
{
	std::string sKey;
	UWLExitCode uec = FindProtectionKey(sKey);
	if (uec != UWLExitCode::SUCCESS) return uec;

	key = StringToWString(sKey);
	return UWLExitCode::SUCCESS;
}

void UberWolfLib::ResetWolfDec()
{
	m_wolfDec.Reset();
}

std::size_t UberWolfLib::RegisterLogCallback(const LogCallback& callback)
{
	return uberLog::AddLogCallback(callback);
}

void UberWolfLib::UnregisterLogCallback(const std::size_t& idx)
{
	uberLog::RemoveLogCallback(idx);
}

void UberWolfLib::RegisterLocQueryFunc(const LocalizerQuery& queryFunc)
{
	uberWolfLib::Localizer::GetInstance().RegisterLocQuery(queryFunc);
}

UWLExitCode UberWolfLib::unpackArchive(const tString& archivePath)
{
	// Make sure the file exists
	if (!fs::exists(archivePath))
		return UWLExitCode::FILE_NOT_FOUND;

	if (archivePath.empty())
		return UWLExitCode::INVALID_PATH;

	if (!m_wolfDec)
		return UWLExitCode::WOLF_DEC_NOT_INITIALIZED;

	const tString fileName = fs::path(archivePath).filename();

	if (!m_wolfDec.IsValidFile(archivePath))
		return UWLExitCode::SUCCESS;

	if (!m_config.override && m_wolfDec.IsAlreadyUnpacked(archivePath))
	{
		INFO_LOG << vFormat(LOCALIZE("unpacked_msg"), fileName) << std::endl;
		return UWLExitCode::SUCCESS;
	}

	INFO_LOG << vFormat(LOCALIZE("unpacking_msg"), fileName);

	bool result = m_wolfDec.UnpackArchive(archivePath, m_config.override);

	INFO_LOG << (result ? LOCALIZE("done_msg") : LOCALIZE("failed_msg")) << std::endl;

	if (!result)
	{
		if (!m_valid)
		{
			if (!findGameFromArchive(archivePath))
				return UWLExitCode::NOT_INITIALIZED;
		}

		UWLExitCode uec = FindDxArcKey();

		if (uec == UWLExitCode::SUCCESS)
			return unpackArchive(archivePath);
	}

	return result ? UWLExitCode::SUCCESS : UWLExitCode::KEY_MISSING;
}

bool UberWolfLib::findDataFolder()
{
	m_dataAsFile = false;

	// Get the folder where the game executable is located
	tString gameFolder = FS_PATH_TO_TSTRING(fs::path(m_gameExePath).parent_path());

	// First check if data.EXT exists in the game folder -- because after unpacking the data folder will exist but contain the wrong files
	if (ExistsWolfDataFile(gameFolder))
	{
		m_dataAsFile = true;
		m_dataFolder = gameFolder;
		return true;
	}

	// Check if the data folder exists
	if (fs::exists(gameFolder + TEXT("/") + DATA_FOLDER_NAME))
	{
		m_dataFolder = gameFolder + TEXT("/") + DATA_FOLDER_NAME;
		return true;
	}

	ERROR_LOG << TEXT("UberWolfLib: Could not find data folder") << std::endl;
	return false;
}

UWLExitCode UberWolfLib::findDxArcKeyFile()
{
	if (!m_wolfPro.IsWolfPro())
		return UWLExitCode::NOT_WOLF_PRO;

	INFO_LOG << LOCALIZE("prot_key_found_msg") << std::endl;

	const Key key = m_wolfPro.GetDxArcKey();

	if (key.empty())
	{
		INFO_LOG << LOCALIZE("prot_key_error_msg") << std::endl;
		return UWLExitCode::KEY_DETECT_FAILED;
	}

	m_wolfDec.AddKey("UNKNOWN_PRO", false, key);
	updateConfig(false, key);
	INFO_LOG << LOCALIZE("det_key_found_msg") << std::endl;

	return UWLExitCode::SUCCESS;
}

UWLExitCode UberWolfLib::findDxArcKeyInject()
{
	INFO_LOG << LOCALIZE("det_key_inj_msg") << std::endl;
	WolfKeyFinder wkf(m_gameExePath);

	const tString tmpPath = FS_PATH_TO_TSTRING(fs::temp_directory_path());

	// Copy the dll into the temp directory
	if (!copyDllFromResource(tmpPath))
	{
		return UWLExitCode::KEY_DETECT_FAILED;
	}

	INFO_LOG << LOCALIZE("exec_game_inj_msg") << std::endl;
	std::vector<BYTE> key;
	if (!wkf.Inject(tmpPath))
	{
		INFO_LOG << LOCALIZE("inj_error_msg") << std::endl;
		return UWLExitCode::KEY_DETECT_FAILED;
	}

	m_wolfDec.AddKey("UNKNOWN", wkf.UseOldDxArc(), wkf.GetKey());
	updateConfig(wkf.UseOldDxArc(), wkf.GetKey());
	INFO_LOG << LOCALIZE("det_key_found_msg") << std::endl;

	return UWLExitCode::SUCCESS;
}

void UberWolfLib::updateConfig(const bool& useOldDxArc, const Key& key)
{
	nlohmann::json data;

	// Load the config file if it exists and is not empty
	if (fs::exists(WolfDec::CONFIG_FILE_NAME) && fs::file_size(WolfDec::CONFIG_FILE_NAME) > 0)
	{
		std::ifstream f(WolfDec::CONFIG_FILE_NAME);
		data = nlohmann::json::parse(f);
	}
	// Create a new config file
	else
	{
		data["keys"] = nlohmann::json::object();
	}

	uint32_t cnt                = 0;
	const std::string NAME_BASE = "UNKNOWN_";
	std::string name            = NAME_BASE + std::to_string(cnt);

	// Check if a key with the name already exists
	while (data["keys"].contains(name))
	{
		cnt++;
		name = NAME_BASE + std::to_string(cnt);
	}

	// Add the key to the config
	data["keys"][name]         = nlohmann::json::object();
	data["keys"][name]["mode"] = (useOldDxArc ? "VER6" : "VER8");
	// Write the key as an array of 0x prefixed hex strings
	data["keys"][name]["key"] = nlohmann::json::array();
	for (const auto& byte : key)
		data["keys"][name]["key"].push_back("0x" + ByteToHexString(byte));

	// Write the config file
	std::ofstream f(WolfDec::CONFIG_FILE_NAME);
	f << data.dump(4);
}

bool UberWolfLib::findGameFromArchive(const tString& archivePath)
{
	// Get the folder containing the archive
	tString searchFolder = FS_PATH_TO_TSTRING(fs::path(archivePath).parent_path());

	// Check if the name of the search folder is the data folder, if search for the game executable in the parent folder
	if (fs::path(searchFolder).filename() == DATA_FOLDER_NAME)
		searchFolder = FS_PATH_TO_TSTRING(fs::path(searchFolder).parent_path());

	INFO_LOG << vFormat(LOCALIZE("search_game_msg"), searchFolder) << std::endl;

	// Check if the game executable exists in the search folder
	for (const auto& gameExeName : GAME_EXE_NAMES)
	{
		const tString gameExePath = searchFolder + TEXT("/") + gameExeName;
		if (fs::exists(gameExePath))
		{
			INFO_LOG << vFormat(LOCALIZE("exe_found_msg"), gameExeName) << std::endl;
			return InitGame(gameExePath);
		}
	}

	INFO_LOG << LOCALIZE("exe_error_msg") << std::endl;

	return false;
}

bool UberWolfLib::copyDllFromResource(const tString& outDir) const
{
	INFO_LOG << vFormat(LOCALIZE("dll_copy_msg"), outDir) << std::endl;

	const HRSRC hResource = FindResource(NULL, MAKEINTRESOURCE(IDR_KEY_HOOK), RT_RCDATA);
	if (hResource == NULL)
	{
		ERROR_LOG << LOCALIZE("dll_error_msg_1") << std::endl;
		return false;
	}

	const HGLOBAL hResData = LoadResource(NULL, hResource);
	if (hResData == NULL)
	{
		ERROR_LOG << LOCALIZE("dll_error_msg_2") << std::endl;
		return false;
	}

	const LPVOID lpResourceData = LockResource(hResData);
	const DWORD dwResourceSize  = SizeofResource(NULL, hResource);

	const tString dllPath = outDir + TEXT("/") + WolfKeyFinder::DLL_NAME;

	HANDLE hFile = CreateFile(dllPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		ERROR_LOG << TEXT("dll_error_msg_3") << std::endl;
		return false;
	}

	DWORD bytesWritten = 0;
	WriteFile(hFile, lpResourceData, dwResourceSize, &bytesWritten, NULL);

	CloseHandle(hFile);
	INFO_LOG << TEXT("dll_copied_msg") << std::endl;

	return true;
}
