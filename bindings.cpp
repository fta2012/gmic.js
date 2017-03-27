#include <emscripten.h>
#include <emscripten/bind.h>

// Note: gmic_inpaint.h is the inpainting parts of gmic.h so we can call them on the CImg even if we aren't including gmic.h
#define cimg_plugin "gmic_inpaint.h"
#include "CImg.h"

#ifdef gmicjs_use_gmic
#include "gmic.h"
#endif

using namespace cimg_library;
using namespace emscripten;

// Conversions between CImg and HTML canvas's ImageData https://developer.mozilla.org/en-US/docs/Web/API/ImageData
// Basically interleaved to non-interleaved rgba
template<typename T>
const CImg<T>& fromImageData(CImg<T>& image, const val& imageDataVal) {
  std::string s = imageDataVal["data"].as<std::string>(); // copy the Uint8ClampedArray into a c++ string
  size_t width = imageDataVal["width"].as<size_t>();
  size_t height = imageDataVal["height"].as<size_t>();
  assert(s.size() == 4 * width * height);
  image.assign(width, height, 1, 4);
  cimg_forXYC(image, x, y, c) {
    image(x, y, 0, c) = static_cast<unsigned char>(s[4 * (x + y * width) + c]); // cast needed for 0-255
  }
  return image;
}
template<typename T>
val toImageData(const CImg<T>& image) {
  size_t width = image.width();
  size_t height = image.height();
  assert(image.depth() == 1);
  unsigned char* imageData = (unsigned char *)malloc(4 * width * height);
  if (image.spectrum() == 4) {
    // RGBA
    cimg_forXYC(image, x, y, c) {
      imageData[4 * (x + y * width) + c] = image(x, y, 0, c);
    }
  } else if (image.spectrum() == 3) {
    // RGB
    cimg_forXY(image, x, y) {
      imageData[4 * (x + y * width) + 0] = image(x, y, 0, 0);
      imageData[4 * (x + y * width) + 1] = image(x, y, 0, 1);
      imageData[4 * (x + y * width) + 2] = image(x, y, 0, 2);
      imageData[4 * (x + y * width) + 3] = 255;
    }
  } else {
    // Grayscale or binary or arbitrary labels?
    // TODO normalize the same way gmic cli does it
    assert(image.spectrum() == 1);
    const T& maxValue = image.max();
    if (maxValue == 1) {
      // Assuming binary
      cimg_forXY(image, x, y) {
        if (image(x, y)) {
          imageData[4 * (x + y * width) + 0] = 255;
          imageData[4 * (x + y * width) + 1] = 0;
          imageData[4 * (x + y * width) + 2] = 0;
        } else {
          imageData[4 * (x + y * width) + 0] = 0;
          imageData[4 * (x + y * width) + 1] = 0;
          imageData[4 * (x + y * width) + 2] = 0;
        }
        imageData[4 * (x + y * width) + 3] = 255;
      }
    } else {
      // Assuming grayscale
      cimg_forXY(image, x, y) {
        const T& value = image(x, y);
        imageData[4 * (x + y * width) + 0] = value;
        imageData[4 * (x + y * width) + 1] = value;
        imageData[4 * (x + y * width) + 2] = value;
        imageData[4 * (x + y * width) + 3] = 255;
      }
    }
  }
  val view(typed_memory_view(4 * width * height, imageData)); // Uint8Array
  val ret = val::global("ImageData").new_(width, height);     // var ret = new ImageData(width, height);
  ret["data"].call<void>("set", view);                        // ret.data.set(view);
  free(imageData);
  return ret;
}

template<typename T>
val toCanvas(const CImg<T>& image) {
  val canvas = val::global("document").call<val>("createElement", val("canvas"));
  canvas.set("width", image.width());
  canvas.set("height", image.height());
  val imageData = toImageData(image);
  canvas.call<val>("getContext", val("2d")).call<val>("putImageData", imageData, 0, 0);
  return canvas;
}

#ifdef gmicjs_use_gmic

val gmicjs(std::string command, const val& imageDataList, const val& imageNameList) {
  assert(val::global("Array").call<bool>("isArray", imageDataList));
  assert(val::global("Array").call<bool>("isArray", imageNameList));

  size_t length = imageDataList["length"].as<size_t>();
  assert(length == imageNameList["length"].as<size_t>());

  gmic_list<char> image_names;
  gmic_list<float> image_list(length);
  for (int i = 0; i < length; i++) {
    std::string imageName = imageNameList[i].as<std::string>();
    gmic_image<char>::string(imageName.c_str()).move_to(image_names);
    printf("Input imageName[%d]: %s\n", i, (const char* const)image_names[i]);

    const val& imageData = imageDataList[i];
    fromImageData<float>(image_list[i], imageData);
    printf("Input imageData[%d]: %d by %d\n", i, image_list[i].width(), image_list[i].height());
  }

  printf("Running command:\n\t%s\n", command.c_str());

  try {
    gmic(command.c_str(), image_list, image_names);
  } catch (gmic_exception &e) {
    std::fprintf(stderr, "ERROR : %s\n", e.what());
  }

  val retImages = val::array();
  val retNames = val::array();
  for (int i = 0; i < image_list.size(); i++) {
    const char* name = image_names[i];
    printf("Output imageName[%d]: %s\n", i, name);
    retNames.call<void>("push", val(name));

    printf("Output imageData[%d]: %dx%dx%d\n", i, image_list[i].width(), image_list[i].height(), image_list[i].spectrum());
    retImages.call<void>("push", toImageData(image_list[i]));
  }

  val ret = val::array();
  ret.call<void>("push", retImages);
  ret.call<void>("push", retNames);
  return ret;
}

#endif // gmicjs_use_gmic

val inpaintPipeline(const val& imageData, const val& maskImageData) {
  // Convert ImageData to CImg
  CImg<unsigned char> image;
  fromImageData<unsigned char>(image, imageData);

  CImg<unsigned char> mask;
  fromImageData<unsigned char>(mask, maskImageData);

  // Construct mask from red pixels
  CImg<unsigned char> binaryMask(mask.width(), mask.height(), 1, 1, 0);
  cimg_forXY(mask, x, y) {
    binaryMask(x, y) = (mask(x, y, 0, 0) == 255 && mask(x, y, 0, 1) == 0 && mask(x, y, 0, 2) == 0);
  }
  // Dilate because HTML canvas's lineTo might not produce exactly (255, 0, 0) due to smoothing
  binaryMask.dilate(3);

  // Inpaint
  image.inpaint_patch(mask);

  // Convert CImg to ImageData
  return toImageData(image);
}


EMSCRIPTEN_BINDINGS(cimg_js_module) {
#ifdef gmicjs_use_gmic
  // Wrapper around the gmic interpreter where ignature is something like:
  //     (String, Array<ImageData>, Array<String>) => [Array<ImageData>, Array<String>]
  function("gmic", &gmicjs);
#endif

  // Example pipeline taking in js image data and outputting js image data using cimg without gmic
  function("inpaintPipeline", &inpaintPipeline);

  /*
   * Binding the CImg class also works but it's a pain to write bindings for all the methods and memory management is also a mess.
   * Example js code looks like:
   *
   *     const cimgMask = new GMIC.CImg();
   *     const cimgMask2 = cimgMask.fromImageData(maskImageData); // Due to limitation of embind this returns a new copy instead of a reference so you need to clean it up!
   *
   *     const cimg = new GMIC.CImg();
   *     const cimg2 = cimg.fromImageData(originalImageData);
   *     const cimg3 = cimg2.inpaint_patch(cimgMask2, 11, 22, 1, 1, 0, 0.5, 0.02, 10, false); // Binding doesn't work with default args
   *
   *     document.body.appendChild(cimgMask.toCanvas());
   *     document.body.appendChild(cimgMask2.toCanvas());
   *     document.body.appendChild(cimg.toCanvas());
   *     document.body.appendChild(cimg2.toCanvas());
   *     document.body.appendChild(cimg3.toCanvas());
   *
   *     // No destructors so have to manually delete
   *     cimgMask.delete();
   *     cimgMask2.delete()
   *     cimg.delete();
   *     cimg2.delete();
   *     cimg3.delete();
   */
  class_<CImg<float>>("CImg")
    .constructor()
    .function("fromImageData", &fromImageData<float>)
    .function("toImageData", &toImageData<float>)
    .function("toCanvas", &toCanvas<float>)
    .function("inpaint_patch", &CImg<float>::inpaint_patch<float>)
    .function("blur", select_overload<CImg<float>&(float, bool, bool)>(&CImg<float>::blur));
}
