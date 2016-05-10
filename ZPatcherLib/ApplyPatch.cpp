//////////////////////////////////////////////////////////////////////////
//
// ZPatcher - Patcher system - Part of the ZUpdater suite
// Felipe "Zoc" Silveira - (c) 2016
//
//////////////////////////////////////////////////////////////////////////
//
// ApplyPatch.cpp
// Functions to apply the Patch data
//
//////////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ApplyPatch.h"
#include "Lzma2Decoder.h"
#include "LogSystem.h"
#include "FileUtils.h"

void ZPatcher::PrintPatchApplyingProgressBar(float Percentage)
{
	int barWidth = 80;

	fprintf(stdout, "\xd[");

	int pos = (int)(barWidth * Percentage / 100.0f);
	for (int i = 0; i < barWidth; ++i)
	{
		if (i < pos) fprintf(stdout, "=");
		else if (i == pos) fprintf(stdout, ">");
		else fprintf(stdout, " ");
	}
	fprintf(stdout, "] %0.2f %%     ", Percentage);
	fflush(stdout);
}

bool ZPatcher::ApplyPatchFile(FILE* patchFile, const std::string& targetPath, std::string previousVersionNumber)
{
	_fseeki64(patchFile, 0LL, SEEK_END);
	long long patchFileSize = _ftelli64(patchFile);
	rewind(patchFile);

	std::string normalizedTargetPath = targetPath;
	NormalizeFileName(normalizedTargetPath);

	size_t fileNameLength = normalizedTargetPath.length();
	if (fileNameLength == 0 || normalizedTargetPath[fileNameLength - 1] != '/')
	{
		Log(LOG_FATAL, "Invalid target directory. It must end either with \\ or /.");
		return false;
	}

	Byte props;
	if (!ReadPatchFileHeader(patchFile, props))
	{
		Log(LOG_FATAL, "Failed to read patch data.");
		return false;
	}

	CLzma2Dec* decoder = InitLzma2Decoder(props);

	std::vector<std::string> backupFileList;
	std::vector<std::string> addedFileList;

	bool success = true;
	for(long long int currentPos = _ftelli64(patchFile); success && (currentPos < patchFileSize); currentPos = _ftelli64(patchFile))
	{
		float percentage = (float)_ftelli64(patchFile) / (float)patchFileSize * 100.0f;
		PrintPatchApplyingProgressBar(percentage);

		std::string outputFile;
		unsigned char operation;
		GetFileinfo(patchFile, outputFile, operation);

		size_t len = outputFile.length();
		switch (static_cast<PatchOperation>(operation))
		{
		case Patch_File_Delete:
			// Check if we are dealing with a directory
			if (outputFile.length() > 0 && outputFile.back() == '/')
			{
				success = success && RemoveOneDirectory(normalizedTargetPath + outputFile);
			}
			else
			{
				success = success && BackupFile(normalizedTargetPath, outputFile, previousVersionNumber);
				if (!success)
					break;
				backupFileList.push_back(outputFile);
				success = success && RemoveFile(normalizedTargetPath + outputFile);
			}
			break;
		case Patch_File_Replace:
			success = success && BackupFile(normalizedTargetPath, outputFile, previousVersionNumber);
			if (!success)
				break;
			backupFileList.push_back(outputFile);
			success = success && RemoveFile(normalizedTargetPath + outputFile);
			success = success && FileDecompress(decoder, patchFile, normalizedTargetPath + outputFile);
			break;
		case Patch_File_Add:
			CreateDirectoryTree(normalizedTargetPath + outputFile);
			success = success && FileDecompress(decoder, patchFile, normalizedTargetPath + outputFile);
			if (success)
				addedFileList.push_back(outputFile);
			break;
		case Patch_Dir_Add:
			CreateDirectoryTree(normalizedTargetPath + outputFile);
			break;
		default:
			Log(LOG_FATAL, "Undefined operation (%d) requested for file %s", static_cast<int>(operation), outputFile.c_str());
			success = false; // Rollback the operation
			break;
		}
	}


	if (success)
	{
		// Everything went fine, so, let's do a backup cleanup
		Log(LOG, "Patching successful! Removing backup directory.");
		std::string backupDirectoryName = normalizedTargetPath + "/" + "backup-" + previousVersionNumber + "/";
		DeleteDirectoryTree(backupDirectoryName);
		PrintPatchApplyingProgressBar(100.0f);
	}
	else
	{
		// Something bad happened. Roll back.
		bool restoreSucess = true;
		Log(LOG_FATAL, "Something went wrong with the patch process! Rolling back!");
		restoreSucess = restoreSucess && RestoreBackup(backupFileList, addedFileList, normalizedTargetPath, previousVersionNumber);

		if (!restoreSucess)
			Log(LOG_FATAL, "At least one file failed to be restored! The application is probably in an inconsistent state!");
	}

	fprintf(stdout, "\n");

	DestroyLzma2Decoder(decoder);

	return success;
}

bool ZPatcher::ApplyPatchFile(const std::string& patchFileName, const std::string& targetPath, std::string previousVersionNumber)
{
	FILE* patchFile;
	errno_t err = 0;

	std::string normalizedPatchFileName = patchFileName;
	NormalizeFileName(normalizedPatchFileName);

	err = fopen_s(&patchFile, normalizedPatchFileName.c_str(), "rb");

	if (err == 0)
	{
		Log(LOG, "Reading patch file %s", normalizedPatchFileName.c_str());
	}
	else
	{
		Log(LOG_FATAL, "Unable to open for reading the patch file %s", normalizedPatchFileName.c_str());
		return false;
	}

	bool success = ApplyPatchFile(patchFile, targetPath, previousVersionNumber);

	fclose(patchFile);

	return success;
}

bool ZPatcher::RestoreBackup(std::vector<std::string>& backupFileList, std::vector<std::string>& addedFileList, const std::string& baseDirectory, std::string previousVersionNumber)
{
	bool result = true;

	for (std::vector<std::string>::iterator itr = backupFileList.begin(); itr < backupFileList.end(); ++itr)
	{
		std::string fullFilename = baseDirectory + "/" + *itr;
		std::string fullBackupFileName = baseDirectory + "/backup-" + previousVersionNumber + "/" + *itr;

		CreateDirectoryTree(fullFilename); // Lazy++;
		result = result && CopyOneFile(fullBackupFileName, fullFilename);		
	}

	for (std::vector<std::string>::iterator itr = addedFileList.begin(); itr < addedFileList.end(); ++itr)
	{
		result = result && RemoveFile(baseDirectory + "/" + *itr);
	}

	return result;
}