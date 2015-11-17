// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "scene_lab/util.h"

#include "fplbase/utilities.h"

#include <sys/stat.h>
#include <cassert>
#include <fstream>
#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#elif !defined(_MSC_VER)
#include <dirent.h>
#endif  // !defined(_MSC_VER)

namespace scene_lab {

std::unordered_map<std::string, time_t> ScanDirectory(
    const std::string& directory, const std::string& file_ext) {
  std::unordered_map<std::string, time_t> file_list;
#if defined(__ANDROID__)
  // Android version uses the asset manager. Let's get that from the Activity.
  JNIEnv* env = fplbase::AndroidGetJNIEnv();
  jobject activity = fplbase::AndroidGetActivity();
  jclass activity_class = env->GetObjectClass(activity);

  jmethodID get_assets = env->GetMethodID(
      activity_class, "getAssets", "()Landroid/content/res/AssetManager;");
  jobject asset_manager_java = env->CallObjectMethod(activity, get_assets);

  AAssetManager* asset_manager =
      AAssetManager_fromJava(env, asset_manager_java);

  // Get file list from the asset manager. There are no timestamps -- but these
  // files won't change anyway, so just force a default timestamp.
  const time_t kDefaultAndroidFileTime = 1;
  const char* kDirSep = "/";

  AAssetDir* dir = AAssetManager_openDir(
      asset_manager, directory.length() == 0 ? "." : directory.c_str());
  if (dir == NULL) return file_list;
  for (;;) {
    const char* next_file = AAssetDir_getNextFileName(dir);
    if (next_file == nullptr) break;
    std::string filename = directory + kDirSep + next_file;
    if (filename.compare(filename.length() - file_ext.length(),
                         file_ext.length(), file_ext) == 0) {
      file_list[filename] = kDefaultAndroidFileTime;
    }
  }

  AAssetDir_close(dir);

  env->DeleteLocalRef(asset_manager_java);
  env->DeleteLocalRef(activity_class);
  env->DeleteLocalRef(activity);

#elif !defined(_MSC_VER)
  const char* kDirSep = "/";
  DIR* dir = opendir(directory.length() == 0 ? "." : directory.c_str());
  if (dir == NULL) return file_list;

  for (;;) {
    dirent* ent = readdir(dir);
    if (ent == nullptr) break;
    std::string filename = directory + kDirSep + ent->d_name;
    if (filename.compare(filename.length() - file_ext.length(),
                         file_ext.length(), file_ext) == 0) {
      struct stat attrib;
      if (stat(filename.c_str(), &attrib) == 0) {
        if (attrib.st_mode & S_IFREG) {
          // Regular file, not a directory.
          time_t modified_time = attrib.st_mtime;
          file_list[filename] = modified_time;
        }
      }
    }
  }
  closedir(dir);
#else
  // dirent.h functionality not supported on Windows.
  // TODO: implement on Windows
  (void)directory;
  (void)fil_ext;
  assert(false);
#endif  // !defined(_MSC_VER)
  return file_list;
}

time_t LoadAssetsIfNewer(time_t threshold,
                         const std::vector<AssetLoader>& asset_loaders) {
  time_t max_time = 0;
  for (auto loader = asset_loaders.begin(); loader != asset_loaders.end();
       ++loader) {
    max_time =
        std::max(max_time, LoadAssetsIfNewer(threshold, loader->directory,
                                             loader->file_extension,
                                             loader->load_function));
  }
  return max_time;
}

time_t LoadAssetsIfNewer(time_t threshold, const std::string& directory,
                         const std::string& file_extension,
                         const AssetLoader::load_function_t& load_function) {
  time_t max_time = 0;
  auto files = scene_lab::ScanDirectory(directory, file_extension);
  for (auto i = files.begin(); i != files.end(); ++i) {
    std::string filename = i->first;
    time_t modtime = i->second;
    if (modtime > threshold) {
      load_function(filename.c_str());
      // Keep track of the latest timestamp of assets.
      max_time = std::max(modtime, max_time);
    }
  }
  return max_time;  // Only non-zero if we actually loaded anything.
}

}  // namespace scene_lab
