#pragma once

#include <Tempest/File>
#include <Tempest/Point>
#include <Tempest/Sprite>

#include <string>
#include <memory>

namespace Tempest {

class Painter;

class FontElement final {
  public:
    FontElement();
    FontElement(std::nullptr_t);
    FontElement(const char* file);
    FontElement(const std::string&    file);
    FontElement(const char16_t*       file);
    FontElement(const std::u16string& file);
    FontElement(const void* data, size_t size);

    class LetterGeometry final {
      public:
        Tempest::Size  size;
        Tempest::Point dpos, advance;
      };

    class Metrics final {
      public:
        int ascent =0;
        int descent=0;
      };

    class Letter final {
      public:
        Tempest::Size   size;
        Tempest::Point  dpos, advance;
        Tempest::Sprite view;
        bool            hasView=false;
      };

    const LetterGeometry& letterGeometry(char32_t ch, float size) const;
    const Letter&         letter(char32_t ch,float size,TextureAtlas& tex) const;

    Size                  textSize(const char* text, float fontSize) const;
    Size                  textSize(const char* text, int maxW, float fontSize) const;
    bool                  isEmpty() const;

    Metrics               metrics(float size) const;

  private:
    template<class CharT>
    FontElement(const CharT* file,std::true_type);

    struct LetterTable;
    struct Impl;
    std::shared_ptr<Impl> ptr;
  };

class Font final {
  public:
    using LetterGeometry = FontElement::LetterGeometry;
    using Letter         = FontElement::Letter;
    using Metrics        = FontElement::Metrics;

    Font()=default;
    Font(const char*           file);
    Font(const std::string&    file);
    Font(const char16_t*       file);
    Font(const std::u16string& file);
    Font(const FontElement& regular, const FontElement& bold,
         const FontElement& italic,  const FontElement& boldItalic);

    void  setPixelSize(float size);
    float pixelSize() const { return size; }

    void  setBold(bool b);
    bool  isBold() const;

    void  setItalic(bool i);
    bool  isItalic() const;

    bool  isEmpty() const;

    Metrics               metrics() const;

    const LetterGeometry& letterGeometry(char16_t ch) const;
    const LetterGeometry& letterGeometry(char32_t ch) const;
    const Letter&         letter(char16_t ch,TextureAtlas& tex) const;
    const Letter&         letter(char32_t ch,TextureAtlas& tex) const;

    const Letter&         letter(char16_t ch,Painter& tex) const;
    const Letter&         letter(char32_t ch,Painter& tex) const;

    Size                  textSize(const char* text) const;
    Size                  textSize(const std::string& text) const;

    Size                  textSize(int maxW, const char* text) const;
    Size                  textSize(int maxW, const std::string& text) const;

  private:
    template<class CharT>
    Font(const CharT* file,std::true_type);

    FontElement fnt[2][2];
    float       size   = 18.f;
    uint8_t     bold   = 0;
    uint8_t     italic = 0;
  };
}
