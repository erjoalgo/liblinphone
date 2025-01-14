/*
 * Copyright (c) 2010-2019 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// TODO: Remove me later.
#include "linphone/core.h"
#include "linphone/utils/utils.h"

#include "content-p.h"
#include "file-transfer-content.h"
#include "bctoolbox/crypto.h"

#include <algorithm>

// =============================================================================

using namespace std;

LINPHONE_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------

class FileTransferContentPrivate : public ContentPrivate {
public:
	string fileName;
	string fileUrl;
	string filePath;
	FileContent *fileContent = nullptr;
	size_t fileSize = 0;
	int fileDuration = 0;
	std::vector<char> fileKey;
	std::vector<char> fileAuthTag;
	ContentType fileContentType;
	~FileTransferContentPrivate() {
		if (!fileKey.empty()) {
			bctbx_clean(fileKey.data(), fileKey.size());
		}
	};
};

// -----------------------------------------------------------------------------

FileTransferContent::FileTransferContent () : Content(*new FileTransferContentPrivate) {
	setContentType(ContentType::FileTransfer);
}

FileTransferContent::FileTransferContent (const FileTransferContent &other) : Content(*new FileTransferContentPrivate) {
	L_D();
	Content::copy(other);
	d->fileName = other.getFileName();
	d->fileUrl = other.getFileUrl();
	d->filePath = other.getFilePath();
	d->fileContent = other.getFileContent();
	d->fileSize = other.getFileSize();
	d->fileKey = other.getFileKey();
	d->fileAuthTag = other.getFileAuthTag();
	d->fileContentType = other.getFileContentType();
	d->fileDuration = other.getFileDuration();
}

FileTransferContent::FileTransferContent (FileTransferContent &&other) : Content(*new FileTransferContentPrivate) {
	L_D();
	Content::copy(other);
	d->fileName = move(other.getPrivate()->fileName);
	d->fileUrl = move(other.getPrivate()->fileUrl);
	d->filePath = move(other.getPrivate()->filePath);
	d->fileContent = move(other.getPrivate()->fileContent);
	d->fileSize = move(other.getPrivate()->fileSize);
	d->fileKey = move(other.getPrivate()->fileKey);
	d->fileAuthTag = move(other.getPrivate()->fileAuthTag);
	d->fileContentType = move(other.getPrivate()->fileContentType);
	d->fileDuration = move(other.getPrivate()->fileDuration);
}

FileTransferContent &FileTransferContent::operator= (const FileTransferContent &other) {
	L_D();
	if (this != &other) {
		Content::operator=(other);
		d->fileName = other.getFileName();
		d->fileUrl = other.getFileUrl();
		d->filePath = other.getFilePath();
		d->fileContent = other.getFileContent();
		d->fileSize = other.getFileSize();
		d->fileKey = other.getFileKey();
		d->fileAuthTag = other.getFileAuthTag();
		d->fileContentType = other.getFileContentType();
		d->fileDuration = other.getFileDuration();
	}

	return *this;
}

FileTransferContent &FileTransferContent::operator= (FileTransferContent &&other) {
	L_D();
	Content::operator=(move(other));
	d->fileName = move(other.getPrivate()->fileName);
	d->fileUrl = move(other.getPrivate()->fileUrl);
	d->filePath = move(other.getPrivate()->filePath);
	d->fileContent = move(other.getPrivate()->fileContent);
	d->fileSize = move(other.getPrivate()->fileSize);
	d->fileKey = move(other.getPrivate()->fileKey);
	d->fileAuthTag = move(other.getPrivate()->fileAuthTag);
	d->fileContentType = move(other.getPrivate()->fileContentType);
	d->fileDuration = move(other.getPrivate()->fileDuration);

	return *this;
}

bool FileTransferContent::operator== (const FileTransferContent &other) const {
	L_D();
	return Content::operator==(other) &&
		d->fileName == other.getFileName() &&
		d->fileUrl == other.getFileUrl() &&
		d->filePath == other.getFilePath() &&
		d->fileSize == other.getFileSize() &&
		d->fileContentType == other.getFileContentType() &&
		d->fileDuration == other.getFileDuration();
}

void FileTransferContent::setFileName (const string &name) {
	L_D();

	d->fileName = Utils::normalizeFilename(name);
}

const string &FileTransferContent::getFileName () const {
	L_D();
	return d->fileName;
}

void FileTransferContent::setFileUrl (const string &url) {
	L_D();
	d->fileUrl = url;
}

const string &FileTransferContent::getFileUrl () const {
	L_D();
	return d->fileUrl;
}

void FileTransferContent::setFilePath (const string &path) {
	L_D();
	d->filePath = path;
}

const string &FileTransferContent::getFilePath () const {
	L_D();
	return d->filePath;
}

void FileTransferContent::setFileContent (FileContent *content) {
	L_D();
	d->fileContent = content;
}

FileContent *FileTransferContent::getFileContent () const {
	L_D();
	return d->fileContent;
}

void FileTransferContent::setFileSize (size_t size) {
	L_D();
	d->fileSize = size;
}

size_t FileTransferContent::getFileSize () const {
	L_D();
	return d->fileSize;
}

void FileTransferContent::setFileDuration (int durationInSeconds) {
	L_D();
	d->fileDuration = durationInSeconds;
}

int FileTransferContent::getFileDuration () const {
	L_D();
	return d->fileDuration;
}

void FileTransferContent::setFileKey (const char *key, size_t size) {
	L_D();
	d->fileKey = vector<char>(key, key + size);
}

const vector<char> &FileTransferContent::getFileKey () const {
	L_D();
	return d->fileKey;
}

size_t FileTransferContent::getFileKeySize() const {
	L_D();
	return d->fileKey.size();
}

void FileTransferContent::setFileAuthTag (const char *tag, size_t size) {
	L_D();
	d->fileAuthTag = vector<char>(tag, tag + size);
}

const vector<char> &FileTransferContent::getFileAuthTag () const {
	L_D();
	return d->fileAuthTag;
}

size_t FileTransferContent::getFileAuthTagSize() const {
	L_D();
	return d->fileAuthTag.size();
}

void FileTransferContent::setFileContentType(const ContentType& contentType) {
	L_D();
	d->fileContentType = contentType;
}

const ContentType& FileTransferContent::getFileContentType () const {
	L_D();
	return d->fileContentType;
}

bool FileTransferContent::isFile () const {
	return false;
}

bool FileTransferContent::isFileTransfer () const {
	return true;
}

bool FileTransferContent::isEncrypted () const {
	L_D();
	return isFileEncrypted(d->filePath);
}

const string FileTransferContent::exportPlainFile() const {
	L_D();
	return exportPlainFileFromEncryptedFile(d->filePath);
}

LINPHONE_END_NAMESPACE
