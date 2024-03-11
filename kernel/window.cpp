#include "window.hpp"
#include "logger.hpp"

Window::Window(int width, int height, PixelFormat shadow_format)
: width_{width}, height_{height} {
    data_.resize(height);
    for (int y = 0; y < height; ++y) {
        data_[y].resize(width);
    }

    FrameBufferConfig config{
        .frame_buffer = nullptr,
        .horizontal_resolution = static_cast<uint32_t>(width),
        .vertical_resolution = static_cast<uint32_t>(height),
        .pixel_format = shadow_format
    };

    if (const Error err = shadow_buffer_.Initialize(config)) {
        Log(kError, "failed to initialize shadow buffer: %s at %s:%d\n",
                err.Name(), err.File(), err.Line());
    }
}

void Window::DrawTo(FrameBuffer& dst, Vector2D<int> position) {
    if (!transparent_color_) {
        dst.Copy(position, shadow_buffer_);
        return;
    }
    
    const auto& tc = transparent_color_.value();
    auto& writer = dst.Writer();
    for (int y = 0; y < Height(); ++y) {
        for (int x = 0; x < Width(); ++x) {
            const auto& c = At(x, y);
            if (c != tc) {
                writer.Write(position + Vector2D<int>{x, y}, c);
            }
        }
    }
}

void Window::SetTransparentColor(std::optional<PixelColor> c) {
    transparent_color_ = c;
}

Window::WindowWriter* Window::Writer() {
    return &writer_;
}

const PixelColor& Window::At(int x, int y) const{
  return data_[y][x];
}

void Window::Write(Vector2D<int> pos, PixelColor c) {
    data_[pos.y][pos.x] = c;
    shadow_buffer_.Writer().Write(pos, c);
}

int Window::Width() const {
  return width_;
}

int Window::Height() const {
  return height_;
}