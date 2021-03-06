// Copyright (c) 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "realsense/enhanced_photography/win/paster_object.h"

#include "realsense/common/win/common_utils.h"
#include "realsense/enhanced_photography/win/common_utils.h"
#include "realsense/enhanced_photography/win/depth_photo_object.h"

namespace realsense {
using namespace realsense::common;  // NOLINT
namespace enhanced_photography {

PasterObject::PasterObject(EnhancedPhotographyInstance* instance)
    : instance_(instance),
      binary_message_size_(0) {
  handler_.Register("getPlanesMap",
      base::Bind(&PasterObject::OnGetPlanesMap,
                 base::Unretained(this)));
  handler_.Register("setPhoto",
      base::Bind(&PasterObject::OnSetPhoto,
                 base::Unretained(this)));
  handler_.Register("setSticker",
      base::Bind(&PasterObject::OnSetSticker,
                 base::Unretained(this)));
  handler_.Register("paste",
      base::Bind(&PasterObject::OnPaste,
                 base::Unretained(this)));
  handler_.Register("previewSticker",
      base::Bind(&PasterObject::OnPreviewSticker,
                 base::Unretained(this)));

  session_ = PXCSession::CreateInstance();
  paster_ = PXCEnhancedPhoto::Paster::CreateInstance(session_);
}

PasterObject::~PasterObject() {
  for (std::vector<PXCImage::ImageData>::iterator it =
      sticker_data_set_.begin(); it != sticker_data_set_.end(); ++it) {
    delete (*it).planes[0];
  }
  if (paster_) {
    paster_->Release();
    paster_ = nullptr;
  }
  if (session_) {
    session_->Release();
    session_ = nullptr;
  }
}

void PasterObject::OnGetPlanesMap(
    scoped_ptr<XWalkExtensionFunctionInfo> info) {
  jsapi::depth_photo::Image image;

  DCHECK(paster_);
  PXCImage* mask = paster_->GetPlanesMap();
  if (!CopyImageToBinaryMessage(mask,
                                binary_message_,
                                &binary_message_size_)) {
    info->PostResult(CreateErrorResult(ERROR_CODE_EXEC_FAILED));
    return;
  }

  scoped_ptr<base::ListValue> result(new base::ListValue());
  result->Append(base::BinaryValue::CreateWithCopiedBuffer(
      reinterpret_cast<const char*>(binary_message_.get()),
      binary_message_size_));
  info->PostResult(result.Pass());
}

void PasterObject::OnSetPhoto(
    scoped_ptr<xwalk::common::XWalkExtensionFunctionInfo> info) {
  DCHECK(paster_);

  scoped_ptr<SetPhoto::Params> params(
      SetPhoto::Params::Create(*info->arguments()));
  if (!params) {
    info->PostResult(CreateErrorResult(ERROR_CODE_PARAM_UNSUPPORTED));
    return;
  }

  std::string object_id = params->photo.object_id;
  DepthPhotoObject* depthPhotoObject = static_cast<DepthPhotoObject*>(
      instance_->GetBindingObjectById(object_id));
  if (!depthPhotoObject || !depthPhotoObject->GetPhoto()) {
    info->PostResult(CreateErrorResult(ERROR_CODE_PHOTO_INVALID));
    return;
  }

  pxcStatus sts = paster_->SetPhoto(depthPhotoObject->GetPhoto());
  if (sts < PXC_STATUS_NO_ERROR) {
    info->PostResult(CreateErrorResult(ERROR_CODE_EXEC_FAILED));
    return;
  }

  info->PostResult(CreateSuccessResult());
}

void PasterObject::OnSetSticker(scoped_ptr<XWalkExtensionFunctionInfo> info) {
  const base::Value* buffer_value = NULL;
  const base::BinaryValue* binary_value = NULL;
  if (info->arguments()->Get(0, &buffer_value) &&
    !buffer_value->IsType(base::Value::TYPE_NULL)) {
    if (!buffer_value->IsType(base::Value::TYPE_BINARY)) {
      info->PostResult(CreateErrorResult(ERROR_CODE_PARAM_UNSUPPORTED));
      return;
    } else {
      binary_value = static_cast<const base::BinaryValue*>(buffer_value);
    }
  } else {
    info->PostResult(CreateErrorResult(ERROR_CODE_PARAM_UNSUPPORTED));
    return;
  }

  DCHECK(paster_);
  const char* data = binary_value->GetBuffer();
  int offset = 0;
  const int* int_array = reinterpret_cast<const int*>(data + offset);
  int width = int_array[0];
  int height = int_array[1];
  offset += 2 * sizeof(int);

  PXCImage::ImageInfo img_info;
  PXCImage::ImageData img_data;
  memset(&img_info, 0, sizeof(img_info));
  memset(&img_data, 0, sizeof(img_data));

  img_info.width = width;
  img_info.height = height;
  img_info.format = PXCImage::PIXEL_FORMAT_RGB32;

  int bufSize = img_info.width * img_info.height * 4;
  img_data.planes[0] = new BYTE[bufSize];
  img_data.pitches[0] = img_info.width * 4;
  img_data.format = img_info.format;

  const uint8_t* image_data = reinterpret_cast<const uint8_t*>(data + offset);
  offset += bufSize;

  for (int y = 0; y < img_info.height; y++) {
    for (int x = 0; x < img_info.width; x++) {
      int i = x * 4 + img_data.pitches[0] * y;
      img_data.planes[0][i] = image_data[i + 2];
      img_data.planes[0][i + 1] = image_data[i + 1];
      img_data.planes[0][i + 2] = image_data[i];
      img_data.planes[0][i + 3] = image_data[i + 3];
    }
  }

  PXCImage* sticker = session_->CreateImage(&img_info, &img_data);
  sticker_data_set_.push_back(img_data);

  int_array = reinterpret_cast<const int*>(data + offset);
  int coordinates_x = int_array[0];
  int coordinates_y = int_array[1];
  offset += 2 * sizeof(int);

  PXCPointI32 coordinates;
  coordinates.x = coordinates_x;
  coordinates.y = coordinates_y;

  PXCEnhancedPhoto::Paster::StickerData params;
  const float_t* float_array = reinterpret_cast<const float_t*>(data + offset);
  params.height = float_array[0];
  params.rotation = float_array[1];
  offset += 2 * sizeof(float_t);

  const uint8_t* bool_array = reinterpret_cast<const uint8_t*>(data + offset);
  params.isCenter = bool_array[0];
  bool has_effects = bool_array[1] != 0;
  // This is for offset alignment (2 bytes plus 2 padding bytes)
  offset += 4;

  pxcStatus sts;
  if (has_effects) {
    PXCEnhancedPhoto::Paster::PasteEffects effects;
    float_array = reinterpret_cast<const float_t*>(data + offset);
    effects.transparency = float_array[0];
    effects.embossHighFreqPass = float_array[1];
    offset += 2 * sizeof(float_t);

    bool_array = reinterpret_cast<const uint8_t*>(data + offset);
    effects.matchIllumination = bool_array[0];
    effects.shadingCorrection = bool_array[1];
    effects.colorCorrection = bool_array[2];
    sts = paster_->SetSticker(sticker, coordinates, &params, &effects);
  } else {
    sts = paster_->SetSticker(sticker, coordinates, &params);
  }

  if (sts != PXC_STATUS_NO_ERROR) {
    info->PostResult(CreateErrorResult(ERROR_CODE_EXEC_FAILED));
    return;
  }

  info->PostResult(CreateSuccessResult());

  sticker->Release();
}

void PasterObject::OnPaste(scoped_ptr<XWalkExtensionFunctionInfo> info) {
  jsapi::depth_photo::Photo photo;

  DCHECK(paster_);
  PXCPhoto* pxcphoto = paster_->Paste();
  if (!pxcphoto) {
    info->PostResult(CreateErrorResult(ERROR_CODE_EXEC_FAILED));
    return;
  }

  CreateDepthPhotoObject(instance_, pxcphoto, &photo);
  info->PostResult(Paste::Results::Create(photo, std::string()));
}

void PasterObject::OnPreviewSticker(
    scoped_ptr<XWalkExtensionFunctionInfo> info) {
  jsapi::depth_photo::Image image;

  DCHECK(paster_);
  PXCImage* mask = paster_->PreviewSticker();
  if (!CopyImageToBinaryMessage(mask,
                                binary_message_,
                                &binary_message_size_)) {
    info->PostResult(CreateErrorResult(ERROR_CODE_EXEC_FAILED));
    return;
  }

  scoped_ptr<base::ListValue> result(new base::ListValue());
  result->Append(base::BinaryValue::CreateWithCopiedBuffer(
      reinterpret_cast<const char*>(binary_message_.get()),
      binary_message_size_));
  info->PostResult(result.Pass());
}

}  // namespace enhanced_photography
}  // namespace realsense
