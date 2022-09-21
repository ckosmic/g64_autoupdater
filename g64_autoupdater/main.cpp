#include <string>
#include <memory>
#include <curl/curl.h>
#include <json/json.h>
#include <zip.h>
#if defined _MSC_VER
#include <direct.h>
#elif defined __GNUC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "GarrysMod/Lua/Interface.h"

#define COPY_BUF_SIZE 2048
#define DEFINE_FUNCTION(x) GlobalLUA->PushCFunction(x); GlobalLUA->SetField(-2, #x);
#define DBG_PRINT( ... ) do { \
    char debugStr[2048]; \
    sprintf_s(debugStr, __VA_ARGS__); \
    GlobalLUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB); \
    GlobalLUA->GetField(-1, "print"); \
    GlobalLUA->PushString(debugStr); \
    GlobalLUA->Call(1, 0); \
    GlobalLUA->Pop(); \
} while(0)
#define REPO_URL "https://api.github.com/repos/ckosmic/g64/releases"

using namespace std;
using namespace GarrysMod::Lua;

ILuaBase* GlobalLUA;
Json::Value releases;
const char zipPath[FILENAME_MAX] = "./libsm64-g64.zip";

size_t str_callback(const char* in, size_t size, size_t num, string* out)
{
	const size_t totalBytes(size * num);
	out->append(in, totalBytes);
	return totalBytes;
}

size_t file_callback(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

int unpack_zip(const char* path, const char* extractPath)
{
	int err = 0;
	zip* z = zip_open(path, 0, &err);

	zip_int64_t numEntries = zip_get_num_entries(z, 0);
	for (zip_int64_t i = 0; i < numEntries; i++)
	{
		struct zip_stat fileStat;

		if (zip_stat_index(z, i, 0, &fileStat))
		{
			DBG_PRINT("[G64 Updater] Failed to stat a file in downloaded zip.");
			return 1;
		}

		if (!(fileStat.valid & ZIP_STAT_NAME))
		{
			// Skipping file with invalid name
			continue;
		}

		if ((fileStat.name[0] != '\0') && (fileStat.name[strlen(fileStat.name) - 1] == '/'))
		{
#if defined _MSC_VER
			if (_mkdir(fileStat.name) && (errno != EEXIST))
#elif defined __GNUC__
			if (mkdir(fileStat.name, 0777) && (errno != EEXIST))
#endif
			{
				DBG_PRINT("[G64 Updater] Error creating directory while unzipping.");
				return 2;
			}
			continue;
		}

		FILE* fp = fopen(fileStat.name, "wb");

		struct zip_file* zipPtr = NULL;
		if ((zipPtr = zip_fopen_index(z, i, 0)) == NULL)
		{
			DBG_PRINT("[G64 Updater] Error extracting file: %s.", zip_strerror(z));
			return 3;
		}

		char buffer[COPY_BUF_SIZE];
		int bytesRead = 0;
		do
		{
			if ((bytesRead = zip_fread(zipPtr, buffer, COPY_BUF_SIZE)) == -1)
			{
				DBG_PRINT("[G64 Updater] Error extracting file: %s.", zip_strerror(z));
				return 4;
			}

			if (bytesRead > 0)
			{
				fwrite(buffer, 1, bytesRead, fp);
			}
		} while (bytesRead > 0);

		zip_fclose(zipPtr);
		zipPtr = NULL;
		fclose(fp);
	}

	if (zip_close(z))
	{
		DBG_PRINT("[G64 Updater] Error closing zip file.");
	}

	if (remove(path))
	{
		DBG_PRINT("[G64 Updater] Error removing zip file.");
	}

	return 0;
}

int download_zip(const char* url, const char* outFile)
{
	CURL* curl = curl_easy_init();
	FILE* fp = fopen(outFile, "wb");

	curl_easy_setopt(curl, CURLOPT_URL, REPO_URL);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	int clRes = fclose(fp);
	if (clRes == 0)
	{
		return unpack_zip(outFile, "./");
	}
	return 1;
}

int get_releases()
{
	const string url = "https://api.github.com/repos/ckosmic/g64/releases";

	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36");
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	long httpCode(0);
	unique_ptr<string> httpData(new string());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, str_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());
	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (httpCode == 200)
	{
		Json::Reader jsonReader;

		if (jsonReader.parse(*httpData.get(), releases))
		{
			//DBG_PRINT("%s", releases[0]["assets"][0]["browser_download_url"].asCString());
		}
		else 
		{
			DBG_PRINT("[G64 Updater] Could not parse GitHub release response data as JSON.");
			return 1;
		}
	}
	else
	{
		DBG_PRINT("[G64 Updater] Failed to request release information.");
		return 2;
	}

	return 0;
}

void parse(int result[4], const string& input)
{
	istringstream parser(input);
	parser >> result[0];
	for (int i = 1; i < 4; i++)
	{
		parser.get();
		parser >> result[i];
	}
}

bool less_than_version(const string& a, const string& b)
{
	int parsedA[4], parsedB[4];
	parse(parsedA, a);
	parse(parsedB, b);
	return lexicographical_compare(parsedA, parsedA + 4, parsedB, parsedB + 4);
}

LUA_FUNCTION(UpdateG64From)
{
	LUA->CheckType(1, Type::String);

	const char* installedVer = LUA->GetString(1);

	int result = get_releases();
	if (result == 0)
	{
		char* remoteVer = (char*)releases[0]["tag_name"].asCString();
		if (remoteVer[0] == 'v')
			memmove(remoteVer, remoteVer + 1, strlen(remoteVer));

		bool needsUpdate = less_than_version(installedVer, remoteVer);
		if (strcmp(installedVer, remoteVer) == 0) needsUpdate = false;
		//DBG_PRINT("[G64 Updater] %s %s %d", installedVer, remoteVer, needsUpdate);
		if (needsUpdate)
		{
			DBG_PRINT("[G64 Updater] An update to G64's library package is available. Downloading version %s...", remoteVer);
			result = download_zip(releases[0]["assets"][0]["browser_download_url"].asCString(), zipPath);
			if (result == 0)
			{
				DBG_PRINT("[G64 Updater] Successfully updated G64 to version %s!", remoteVer);
				LUA->PushNumber(1);
			}
			else
			{
				DBG_PRINT("[G64 Updater] Failed to update G64's library package.");
				LUA->PushNumber(result);
			}
		}
		else
		{
			DBG_PRINT("[G64 Updater] The G64 library package is up to date! (version %s)", installedVer);
			LUA->PushNumber(0);
		}
	} 
	else 
	{
		DBG_PRINT("[G64 Updater] Failed to update G64's library package.");
		LUA->PushNumber(2);
	}

	return 1;
}

LUA_FUNCTION(LessThanVersion)
{
	LUA->CheckType(1, Type::String);
	LUA->CheckType(2, Type::String);

	const string ver1 = string(LUA->GetString(1));
	const string ver2 = string(LUA->GetString(2));

	LUA->PushBool(less_than_version(ver1, ver2));

	return 1;
}

LUA_FUNCTION(GetReleases)
{
	get_releases();

	return 0;
}

LUA_FUNCTION(DownloadZip)
{
	download_zip(releases[0]["assets"][0]["browser_download_url"].asCString(), zipPath);

	return 0;
}

GMOD_MODULE_OPEN()
{
	GlobalLUA = LUA;

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->CreateTable();
		DEFINE_FUNCTION(GetReleases);
		DEFINE_FUNCTION(DownloadZip);
		DEFINE_FUNCTION(LessThanVersion);
		DEFINE_FUNCTION(UpdateG64From);
	LUA->SetField(-2, "g64updater");
	LUA->Pop();

	return 0;
}

GMOD_MODULE_CLOSE()
{
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->PushNil();
	LUA->SetField(-2, "g64updater");
	LUA->Pop();

	return 0;
}