// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/google_apis/drive_uploader.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/values.h"
#include "chrome/browser/google_apis/drive_service_interface.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace google_apis {

namespace {

const char kTestDummyId[] = "file:dummy_id";
const char kTestDocumentTitle[] = "Hello world";
const char kTestDrivePath[] = "drive/dummy.txt";
const char kTestInitialUploadURL[] =
    "http://test/feeds/upload/create-session/default/private/full";
const char kTestMimeType[] = "text/plain";
const char kTestUploadURL[] = "http://test/upload_location";
const int64 kUploadChunkSize = 512 * 1024;

// Creates a |size| byte file and returns its |path|. The file is filled with
// random bytes so that the test assertions can identify correct
// portion of the file is being sent.
bool CreateFileOfSpecifiedSize(const FilePath& temp_dir,
                               size_t size,
                               FilePath* path,
                               std::string* data) {
  data->resize(size);
  for (size_t i = 0; i < size; ++i)
    (*data)[i] = static_cast<char>(rand() % 256);
  if (!file_util::CreateTemporaryFileInDir(temp_dir, path))
    return false;
  return file_util::WriteFile(*path, data->c_str(), static_cast<int>(size)) ==
      static_cast<int>(size);
}

// Base class for mock DriveService that implements by NOTREACHED() the
// methods that should not be used from DriveUploader.
class MockDriveServiceBase : public DriveServiceInterface {
  // DriveServiceInterface overrides.
  virtual void Initialize(Profile* profile) OVERRIDE {
    NOTREACHED();
  }
  virtual void AddObserver(DriveServiceObserver* observer) OVERRIDE {
    NOTREACHED();
  }
  virtual void RemoveObserver(DriveServiceObserver* observer) OVERRIDE {
    NOTREACHED();
  }
  virtual bool CanStartOperation() const OVERRIDE {
    NOTREACHED();
    return false;
  }
  virtual void CancelAll() OVERRIDE {
    NOTREACHED();
  }
  virtual bool CancelForFilePath(const FilePath& file_path) OVERRIDE {
    NOTREACHED();
    return false;
  }
  virtual OperationProgressStatusList GetProgressStatusList() const OVERRIDE {
    NOTREACHED();
    return OperationProgressStatusList();
  }
  virtual bool HasAccessToken() const OVERRIDE {
    NOTREACHED();
    return false;
  }
  virtual bool HasRefreshToken() const OVERRIDE {
    NOTREACHED();
    return false;
  }
  virtual void GetDocuments(const GURL& feed_url,
                            int64 start_changestamp,
                            const std::string& search_query,
                            bool shared_with_me,
                            const std::string& directory_resource_id,
                            const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void GetDocumentEntry(const std::string& resource_id,
                                const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void GetAccountMetadata(const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void GetApplicationInfo(const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void DeleteDocument(const GURL& document_url,
                              const EntryActionCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void DownloadDocument(
      const FilePath& virtual_path,
      const FilePath& local_cache_path,
      const GURL& content_url,
      DocumentExportFormat format,
      const DownloadActionCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void CopyDocument(const std::string& resource_id,
                            const FilePath::StringType& new_name,
                            const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void RenameResource(const GURL& resource_url,
                              const FilePath::StringType& new_name,
                              const EntryActionCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void AddResourceToDirectory(
      const GURL& parent_content_url,
      const GURL& resource_url,
      const EntryActionCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void RemoveResourceFromDirectory(
      const GURL& parent_content_url,
      const std::string& resource_id,
      const EntryActionCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void AddNewDirectory(const GURL& parent_content_url,
                               const FilePath::StringType& directory_name,
                               const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void DownloadFile(
      const FilePath& virtual_path,
      const FilePath& local_cache_path,
      const GURL& content_url,
      const DownloadActionCallback& download_action_callback,
      const GetContentCallback& get_content_callback) OVERRIDE {
    NOTREACHED();
  }
  virtual void AuthorizeApp(const GURL& resource_url,
                            const std::string& app_id,
                            const GetDataCallback& callback) OVERRIDE {
    NOTREACHED();
  }
};

// Mock DriveService that verifies if the uploaded content matches the preset
// expectation.
class MockDriveServiceWithUploadExpectation : public MockDriveServiceBase {
 public:
  // Sets up an expected upload content. InitiateUpload and ResumeUpload will
  // verify that the specified data is correctly uploaded.
  explicit MockDriveServiceWithUploadExpectation(
      const std::string& expected_upload_content)
     : expected_upload_content_(expected_upload_content),
       received_bytes_(0),
       resume_upload_call_count_(0) {}

  int64 received_bytes() const { return received_bytes_; }

  int64 resume_upload_call_count() const { return resume_upload_call_count_; }

 private:
  // DriveServiceInterface overrides.
  // Handles a request for obtaining an upload location URL.
  virtual void InitiateUpload(const InitiateUploadParams& params,
                              const InitiateUploadCallback& callback) OVERRIDE {
    const int64 expected_size = expected_upload_content_.size();

    // Verify that the expected parameters are passed.
    if (params.upload_mode == UPLOAD_NEW_FILE)
      EXPECT_EQ(kTestDocumentTitle, params.title);
    else
      EXPECT_EQ("", params.title);
    EXPECT_EQ(kTestMimeType, params.content_type);
    EXPECT_EQ(expected_size, params.content_length);
    EXPECT_EQ(GURL(kTestInitialUploadURL), params.upload_location);

    // Calls back the upload URL for subsequent ResumeUpload operations.
    // InitiateUpload is an asynchronous function, so don't callback directly.
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(callback, HTTP_SUCCESS, GURL(kTestUploadURL)));
  }

 // Handles a request for uploading a chunk of bytes.
  virtual void ResumeUpload(const ResumeUploadParams& params,
                            const ResumeUploadCallback& callback) OVERRIDE {
    const int64 expected_size = expected_upload_content_.size();

    // The upload range should start from the current first unreceived byte.
    EXPECT_EQ(received_bytes_, params.start_range);

    // The upload data must be split into 512KB chunks.
    const int64 expected_chunk_end =
        std::min(received_bytes_ + kUploadChunkSize, expected_size);
    EXPECT_EQ(expected_chunk_end - 1, params.end_range);

    const int64 expected_chunk_size =
        expected_chunk_end - received_bytes_;
    const std::string expected_chunk_data(
        expected_upload_content_.substr(received_bytes_,
                                        expected_chunk_size));
    std::string uploading_data(params.buf->data(),
                               params.buf->data() + expected_chunk_size);
    EXPECT_EQ(expected_chunk_data, uploading_data);

    // The upload URL returned by InitiateUpload() must be used.
    EXPECT_EQ(GURL(kTestUploadURL), params.upload_location);

    // Other parameters should be the exact values passed to DriveUploader.
    EXPECT_EQ(expected_size, params.content_length);
    EXPECT_EQ(kTestMimeType, params.content_type);

    // Update the internal status of the current upload session.
    resume_upload_call_count_ ++;
    received_bytes_ = params.end_range + 1;

    // Callback with response.
    ResumeUploadResponse response;
    scoped_ptr<DocumentEntry> entry;
    if (received_bytes_ == params.content_length) {
      response = ResumeUploadResponse(
          params.upload_mode == UPLOAD_NEW_FILE ? HTTP_CREATED : HTTP_SUCCESS,
          -1, -1);

      base::DictionaryValue dict;
      dict.Set("id.$t", new base::StringValue(kTestDummyId));
      entry = DocumentEntry::CreateFrom(dict);
    } else {
      response = ResumeUploadResponse(HTTP_RESUME_INCOMPLETE, 0,
                                      params.end_range);
    }
    // ResumeUpload is an asynchronous function, so don't callback directly.
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(callback, response, base::Passed(&entry)));
  }

  std::string expected_upload_content_;
  int64 received_bytes_;
  int64 resume_upload_call_count_;
};

// Mock DriveService that returns a failure at InitiateUpload().
class MockDriveServiceNoConnectionAtInitiate : public MockDriveServiceBase {
  // Returns error.
  virtual void InitiateUpload(const InitiateUploadParams& params,
                              const InitiateUploadCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(callback, GDATA_NO_CONNECTION, GURL()));
  }

  // Should not be used.
  virtual void ResumeUpload(const ResumeUploadParams& params,
                            const ResumeUploadCallback& callback) OVERRIDE {
    NOTREACHED();
  }
};

// Mock DriveService that returns a failure at ResumeUpload().
class MockDriveServiceNoConnectionAtResume : public MockDriveServiceBase {
  // Succeeds and returns an upload location URL.
  virtual void InitiateUpload(const InitiateUploadParams& params,
                              const InitiateUploadCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(callback, HTTP_SUCCESS, GURL(kTestInitialUploadURL)));
  }

  // Returns error.
  virtual void ResumeUpload(const ResumeUploadParams& params,
                            const ResumeUploadCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(callback,
                   ResumeUploadResponse(GDATA_NO_CONNECTION, -1, -1),
                   base::Passed(scoped_ptr<DocumentEntry>())));
  }
};

class DriveUploaderTest : public testing::Test {
 public:
  DriveUploaderTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        io_thread_(content::BrowserThread::IO) {
  }

  virtual void SetUp() OVERRIDE {
    io_thread_.StartIOThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  virtual void TearDown() OVERRIDE {
    ASSERT_TRUE(temp_dir_.Delete());
  }

 protected:
  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread io_thread_;
  base::ScopedTempDir temp_dir_;
};

// Records whether UploaderReadyCallback is called or not.
void OnUploaderReady(bool* called, int32 /* upload_id */) {
  *called = true;
}

// Struct for holding the results copied from UploadCompletionCallback.
struct UploadCompletionCallbackResult {
  UploadCompletionCallbackResult() : error(DRIVE_UPLOAD_ERROR_ABORT) {}
  DriveUploadError error;
  FilePath drive_path;
  FilePath file_path;
  scoped_ptr<DocumentEntry> document_entry;
};

// Copies the result from UploadCompletionCallback and quit the message loop.
void CopyResultsFromUploadCompletionCallbackAndQuit(
    UploadCompletionCallbackResult* out,
    DriveUploadError error,
    const FilePath& drive_path,
    const FilePath& file_path,
    scoped_ptr<DocumentEntry> document_entry) {
  out->error = error;
  out->drive_path = drive_path;
  out->file_path = file_path;
  out->document_entry = document_entry.Pass();
  MessageLoop::current()->Quit();
}

}  // namespace

TEST_F(DriveUploaderTest, UploadExisting0KB) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 0,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;

  MockDriveServiceWithUploadExpectation mock_service(data);
  DriveUploader uploader(&mock_service);
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestMimeType,
      0,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  EXPECT_EQ(1, mock_service.resume_upload_call_count());
  EXPECT_EQ(0, mock_service.received_bytes());
  EXPECT_EQ(DRIVE_UPLOAD_OK, out.error);
  EXPECT_EQ(FilePath::FromUTF8Unsafe(kTestDrivePath), out.drive_path);
  EXPECT_EQ(local_path, out.file_path);
  ASSERT_TRUE(out.document_entry);
  EXPECT_EQ(kTestDummyId, out.document_entry->id());
}

TEST_F(DriveUploaderTest, UploadExisting512KB) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 512 * 1024,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;

  MockDriveServiceWithUploadExpectation mock_service(data);
  DriveUploader uploader(&mock_service);
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestMimeType,
      512 * 1024,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  // 512KB upload should not be split into multiple chunks.
  EXPECT_EQ(1, mock_service.resume_upload_call_count());
  EXPECT_EQ(512 * 1024, mock_service.received_bytes());
  EXPECT_EQ(DRIVE_UPLOAD_OK, out.error);
  EXPECT_EQ(FilePath::FromUTF8Unsafe(kTestDrivePath), out.drive_path);
  EXPECT_EQ(local_path, out.file_path);
  ASSERT_TRUE(out.document_entry);
  EXPECT_EQ(kTestDummyId, out.document_entry->id());
}

TEST_F(DriveUploaderTest, UploadExisting1234KB) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 1234 * 1024,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;

  MockDriveServiceWithUploadExpectation mock_service(data);
  DriveUploader uploader(&mock_service);
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestMimeType,
      1234 * 1024,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  // The file should be split into 3 chunks (1234 = 512 + 512 + 210).
  EXPECT_EQ(3, mock_service.resume_upload_call_count());
  EXPECT_EQ(1234 * 1024, mock_service.received_bytes());
  EXPECT_EQ(DRIVE_UPLOAD_OK, out.error);
  EXPECT_EQ(FilePath::FromUTF8Unsafe(kTestDrivePath), out.drive_path);
  EXPECT_EQ(local_path, out.file_path);
  ASSERT_TRUE(out.document_entry);
  EXPECT_EQ(kTestDummyId, out.document_entry->id());
}

TEST_F(DriveUploaderTest, UploadNew1234KB) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 1234 * 1024,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;
  bool uploader_ready_called = false;

  MockDriveServiceWithUploadExpectation mock_service(data);
  DriveUploader uploader(&mock_service);
  uploader.UploadNewFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestDocumentTitle,
      kTestMimeType,
      1234 * 1024,  // content length
      1234 * 1024,  // current file size
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out),
      base::Bind(&OnUploaderReady, &uploader_ready_called)
  );
  message_loop_.Run();

  EXPECT_TRUE(uploader_ready_called);
  // The file should be split into 3 chunks (1234 = 512 + 512 + 210).
  EXPECT_EQ(3, mock_service.resume_upload_call_count());
  EXPECT_EQ(1234 * 1024, mock_service.received_bytes());
  EXPECT_EQ(DRIVE_UPLOAD_OK, out.error);
  EXPECT_EQ(FilePath::FromUTF8Unsafe(kTestDrivePath), out.drive_path);
  EXPECT_EQ(local_path, out.file_path);
  ASSERT_TRUE(out.document_entry);
  EXPECT_EQ(kTestDummyId, out.document_entry->id());
}

TEST_F(DriveUploaderTest, InitiateUploadFail) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 512 * 1024,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;

  MockDriveServiceNoConnectionAtInitiate mock_service;
  DriveUploader uploader(&mock_service);
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestMimeType,
      512 * 1024,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  EXPECT_EQ(DRIVE_UPLOAD_ERROR_ABORT, out.error);
}

TEST_F(DriveUploaderTest, ResumeUploadFail) {
  FilePath local_path;
  std::string data;
  ASSERT_TRUE(CreateFileOfSpecifiedSize(temp_dir_.path(), 512 * 1024,
                                        &local_path, &data));

  UploadCompletionCallbackResult out;

  MockDriveServiceNoConnectionAtResume mock_service;
  DriveUploader uploader(&mock_service);
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      local_path,
      kTestMimeType,
      512 * 1024,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  EXPECT_EQ(DRIVE_UPLOAD_ERROR_ABORT, out.error);
}

TEST_F(DriveUploaderTest, NonExistingSourceFile) {
  UploadCompletionCallbackResult out;

  DriveUploader uploader(NULL);  // NULL, the service won't be used.
  uploader.UploadExistingFile(
      GURL(kTestInitialUploadURL),
      FilePath::FromUTF8Unsafe(kTestDrivePath),
      temp_dir_.path().AppendASCII("_this_path_should_not_exist_"),
      kTestMimeType,
      0,  // content length
      base::Bind(&CopyResultsFromUploadCompletionCallbackAndQuit, &out));
  message_loop_.Run();

  // Should return failure without doing any attempt to connect to the server.
  EXPECT_EQ(DRIVE_UPLOAD_ERROR_NOT_FOUND, out.error);
}

}  // namespace google_apis
