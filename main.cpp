#include <algorithm>
#include <cerrno>
#include <cstdint> // std::uint32_t
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <termios.h>
#include <unistd.h>

#include <boost/iterator/iterator_facade.hpp>

#include <cairo/cairo-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#ifndef NDEBUG
#define PRINT(NAME, ...) std::cerr << #NAME __VA_ARGS__ << std::endl;
#else
#define PRINT(NAME, ...)
#endif // NDEBUG

namespace xcb
{
  struct shared_connection
    : public std::shared_ptr<xcb_connection_t>
  {
    explicit shared_connection()
      : std::shared_ptr<xcb_connection_t> {xcb_connect(nullptr, nullptr), xcb_disconnect}
    {
      if (const auto state {xcb_connection_has_error(*this)}; state) switch (state)
      {
      case XCB_CONN_ERROR:
        throw std::runtime_error {"socket errors, pipe errors or other stream errors"};

      case XCB_CONN_CLOSED_EXT_NOTSUPPORTED:
        throw std::runtime_error {"extension not supported"};

      case XCB_CONN_CLOSED_MEM_INSUFFICIENT:
        throw std::runtime_error {"memory not available"};

      case XCB_CONN_CLOSED_REQ_LEN_EXCEED:
        throw std::runtime_error {"exceeding request length that server accepts"};

      case XCB_CONN_CLOSED_PARSE_ERR:
        throw std::runtime_error {"error during parsing display string"};

      case XCB_CONN_CLOSED_INVALID_SCREEN:
        throw std::runtime_error {"the server does not have a screen matching the display"};
      }
    }

    operator xcb_connection_t*() const noexcept
    {
      return get();
    }

    decltype(auto) flush() const noexcept
    {
      return xcb_flush(*this);
    }
  };

  #define XCB_ITERATOR(NAME)                                                   \
  class iterator                                                               \
    : public xcb_##NAME##_iterator_t                                           \
    , public boost::iterator_facade<                                           \
               iterator, xcb_##NAME##_t, boost::forward_traversal_tag          \
             >                                                                 \
  {                                                                            \
    friend class boost::iterator_core_access;                                  \
                                                                               \
    void increment() noexcept                                                  \
    {                                                                          \
      xcb_##NAME##_next(this);                                                 \
    }                                                                          \
                                                                               \
    reference dereference() const noexcept                                     \
    {                                                                          \
      return *data;                                                            \
    }                                                                          \
                                                                               \
    template <typename T,                                                      \
              typename = typename std::enable_if<                              \
                           std::is_convertible<                                \
                             T, xcb_##NAME##_iterator_t                        \
                           >::value                                            \
                         >::type>                                              \
    bool equal(const T& rhs) const noexcept                                    \
    {                                                                          \
      return data == rhs.data;                                                 \
    }                                                                          \
                                                                               \
  public:                                                                      \
    constexpr iterator(const xcb_##NAME##_iterator_t& other)                   \
      : xcb_##NAME##_iterator_t {other}                                        \
    {}                                                                         \
                                                                               \
    constexpr iterator(const xcb_generic_iterator_t& generic)                  \
      : xcb_##NAME##_iterator_t {                                              \
          reinterpret_cast<const xcb_##NAME##_iterator_t&>(generic)            \
        }                                                                      \
    {}                                                                         \
  };

  #define XCB_PROTOCOL(NAME, ITERATOR, TARGET)                                 \
  struct NAME##s                                                               \
  {                                                                            \
    XCB_ITERATOR(ITERATOR)                                                     \
                                                                               \
    using const_iterator = const iterator;                                     \
                                                                               \
    const xcb_##NAME##_t* data;                                                \
                                                                               \
    explicit NAME##s(const xcb_##NAME##_t* data)                               \
      : data {data}                                                            \
    {}                                                                         \
                                                                               \
    const_iterator cbegin() const noexcept                                     \
    {                                                                          \
      return xcb_##NAME##_##TARGET##_iterator(data);                           \
    }                                                                          \
                                                                               \
    iterator begin() const noexcept                                            \
    {                                                                          \
      return cbegin();                                                         \
    }                                                                          \
                                                                               \
    const_iterator cend() const noexcept                                       \
    {                                                                          \
      return xcb_##ITERATOR##_end(                                             \
               xcb_##NAME##_##TARGET##_iterator(data)                          \
             );                                                                \
    }                                                                          \
                                                                               \
    iterator end() const noexcept                                              \
    {                                                                          \
      return cend();                                                           \
    }                                                                          \
                                                                               \
    auto size() const noexcept                                                 \
    {                                                                          \
      return xcb_##NAME##_##TARGET##_length(data);                             \
    }                                                                          \
                                                                               \
    auto empty() const noexcept                                                \
    {                                                                          \
      return not size();                                                       \
    }                                                                          \
                                                                               \
    operator xcb_##NAME##_t() const noexcept                                   \
    {                                                                          \
      return *data;                                                            \
    }                                                                          \
  };

  XCB_PROTOCOL(setup, screen, roots)
  XCB_PROTOCOL(screen, depth, allowed_depths)
  XCB_PROTOCOL(depth, visualtype, visuals)

  auto root_screen(const shared_connection& connection)
  {
    return std::begin(setups {xcb_get_setup(connection)})->root;
  }

  // XXX cairo_xcb_surface_create requires non-const xcb_visualtype_t*
  auto root_visual(const shared_connection& connection)
  {
    for (const auto& screen : setups {xcb_get_setup(connection)})
    {
      for (const auto& depth : screens {&screen})
      {
        for (auto&& visual : depths {&depth})
        {
          if (screen.root_visual == visual.visual_id)
          {
            return &visual;
          }
        }
      }
    }

    throw std::runtime_error {"there is no root visualtype"};
  }

  // http://manpages.ubuntu.com/manpages/bionic/man3/xcb_create_window.3.html
  struct identity
  {
    static inline const shared_connection connection {};

    const xcb_window_t value;

    explicit identity(const xcb_window_t& parent = root_screen(connection))
      : value {xcb_generate_id(connection)}
    {
      xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT, // depth
        value, // child window (this) identity
        parent, // parent window identity
        std::max(0, 0), // x
        std::max(0, 0), // y
        std::max(1, 1), // width (at least 1 required)
        std::max(1, 1), // height (at least 1 required)
        std::max(2, 0), // border width
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        XCB_COPY_FROM_PARENT,
        0, // window attributes
        nullptr
      );
    }

    ~identity()
    {
      xcb_destroy_window(connection, value);
    }

    decltype(auto) map() const noexcept
    {
      return xcb_map_window(connection, value);
    }

    decltype(auto) unmap() const
    {
      return xcb_unmap_window(connection, value);
    }

    template <typename... Ts>
    decltype(auto) configure(const std::uint16_t mask, Ts&&... configurations) const
    {
      const std::vector<std::uint32_t> values {std::forward<decltype(configurations)>(configurations)...};
      return xcb_configure_window(connection, value, mask, values.data());
    }

    template <typename Mask, typename... Ts>
    decltype(auto) change_attributes(const Mask& mask, Ts&&... attributes) const
    {
      const std::vector<std::uint32_t> values {std::forward<decltype(attributes)>(attributes)...};
      return xcb_change_window_attributes(connection, value, mask, values.data());
    }
  };

  struct event
    : public std::unique_ptr<xcb_generic_event_t>
  {
    template <typename... Ts>
    explicit event(Ts&&... operands)
      : std::unique_ptr<xcb_generic_event_t> {std::forward<decltype(operands)>(operands)...}
    {}

    auto type() const noexcept
    {
      return get()->response_type & ~0x80;
    }

    decltype(auto) wait(const shared_connection& connection)
    {
      reset(xcb_wait_for_event(connection));
      return *this;
    }

    template <typename T>
    auto release_as()
    {
      return std::unique_ptr<T> {reinterpret_cast<T*>(release())};
    }
  };

  template <typename Surface, auto EventMask>
  struct machine
    : public identity
  {
    template <typename... Ts>
    explicit machine(Ts&&... operands)
      : identity {std::forward<decltype(operands)>(operands)...}
    {}

    #define TRANSFER_EVENT(EVENT_NAME)                                           \
    if constexpr (std::is_invocable<                                             \
                    Surface, std::unique_ptr<xcb_##EVENT_NAME##_event_t>         \
                  >::value)                                                      \
    {                                                                            \
      PRINT(EVENT_NAME, "")                                                      \
      static_cast<Surface&>(*this)(                                              \
        event.release_as<xcb_##EVENT_NAME##_event_t>()                           \
      );                                                                         \
    }                                                                            \
    else                                                                         \
    {                                                                            \
      PRINT(EVENT_NAME, " (unimplemented)")                                      \
    }                                                                            \
    break;

    void execute()
    {
      change_attributes(XCB_CW_EVENT_MASK, EventMask);
      connection.flush();

      for (event event {nullptr}; event.wait(connection); connection.flush())
      {
        #ifndef NDEBUG
        std::cerr << "; execution\t; sequence " << event->sequence << " on " << this << ", ";
        #endif

        switch (event.type())
        {
        case XCB_KEY_PRESS:                                                  //  2
          TRANSFER_EVENT(key_press)

        case XCB_KEY_RELEASE:                                                //  3
          TRANSFER_EVENT(key_release)

        case XCB_BUTTON_PRESS:                                               //  4
          TRANSFER_EVENT(button_press)

        case XCB_BUTTON_RELEASE:                                             //  5
          TRANSFER_EVENT(button_release)

        case XCB_MOTION_NOTIFY:                                              //  6
          TRANSFER_EVENT(motion_notify)

        case XCB_ENTER_NOTIFY:                                               //  7
          TRANSFER_EVENT(enter_notify)

        case XCB_LEAVE_NOTIFY:                                               //  8
          TRANSFER_EVENT(leave_notify)

        case XCB_FOCUS_IN:                                                   //  9
          TRANSFER_EVENT(focus_in)

        case XCB_FOCUS_OUT:                                                  // 10
          TRANSFER_EVENT(focus_out)

        case XCB_KEYMAP_NOTIFY:                                              // 11
          TRANSFER_EVENT(keymap_notify)

        case XCB_EXPOSE:                                                     // 12
          TRANSFER_EVENT(expose)

        case XCB_GRAPHICS_EXPOSURE:                                          // 13
          TRANSFER_EVENT(graphics_exposure)

        case XCB_NO_EXPOSURE:                                                // 14
          TRANSFER_EVENT(no_exposure)

        case XCB_VISIBILITY_NOTIFY:                                          // 15
          TRANSFER_EVENT(visibility_notify)

        case XCB_CREATE_NOTIFY:                                              // 16
          TRANSFER_EVENT(create_notify)

        case XCB_DESTROY_NOTIFY:                                             // 17
          TRANSFER_EVENT(destroy_notify)

        case XCB_UNMAP_NOTIFY:                                               // 18
          TRANSFER_EVENT(unmap_notify)

        case XCB_MAP_NOTIFY:                                                 // 19
          TRANSFER_EVENT(map_notify)

        case XCB_MAP_REQUEST:                                                // 20
          TRANSFER_EVENT(map_request)

        case XCB_REPARENT_NOTIFY:                                            // 21
          TRANSFER_EVENT(reparent_notify)

        case XCB_CONFIGURE_NOTIFY:                                           // 22
          TRANSFER_EVENT(configure_notify)

        case XCB_CONFIGURE_REQUEST:                                          // 23
          TRANSFER_EVENT(configure_request)

        case XCB_GRAVITY_NOTIFY:                                             // 24
          TRANSFER_EVENT(gravity_notify)

        case XCB_RESIZE_REQUEST:                                             // 25
          TRANSFER_EVENT(resize_request)

        case XCB_CIRCULATE_NOTIFY:                                           // 26
          TRANSFER_EVENT(circulate_notify)

        case XCB_CIRCULATE_REQUEST:                                          // 27
          TRANSFER_EVENT(circulate_request)

        case XCB_PROPERTY_NOTIFY:                                            // 28
          TRANSFER_EVENT(property_notify)

        case XCB_SELECTION_CLEAR:                                            // 29
          TRANSFER_EVENT(selection_clear)

        case XCB_SELECTION_REQUEST:                                          // 30
          TRANSFER_EVENT(selection_request)

        case XCB_SELECTION_NOTIFY:                                           // 31
          TRANSFER_EVENT(selection_notify)

        case XCB_COLORMAP_NOTIFY:                                            // 32
          TRANSFER_EVENT(colormap_notify)

        case XCB_CLIENT_MESSAGE:                                             // 33
          TRANSFER_EVENT(client_message)

        case XCB_MAPPING_NOTIFY:                                             // 34
          TRANSFER_EVENT(mapping_notify)

        case XCB_GE_GENERIC:                                                 // 35
          TRANSFER_EVENT(ge_generic)
        }
      }
    }
  };

  template <typename Char>
  class keyboard
  {
    std::unique_ptr<xcb_key_symbols_t, decltype(&xcb_key_symbols_free)> symbols;

  public:
    explicit keyboard(const shared_connection& connection)
      : symbols {xcb_key_symbols_alloc(connection), xcb_key_symbols_free}
    {}

    auto press(const std::unique_ptr<xcb_key_press_event_t> event)
    {
      auto code {xcb_key_press_lookup_keysym(
        symbols.get(),
        event.get(),
        event->state & ~XCB_MOD_MASK_CONTROL
      )};

      if (xcb_is_modifier_key(code))
      {
        return false;
      }

      switch (event->state)
      {
      case XCB_MOD_MASK_CONTROL:
      case XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT:
        switch (code)
        {
        #define DEBUG_KEYCODE(CODE, ...) \
        std::cerr << "; keybord\t; caret " #CODE " - " __VA_ARGS__ << std::endl;

        case '@': DEBUG_KEYCODE(0x00, "null")                      break;
        case 'A': DEBUG_KEYCODE(0x01, "start-of-heading")          break;
        case 'B': DEBUG_KEYCODE(0x02, "start-of-text")             break;
        case 'C': DEBUG_KEYCODE(0x03, "end-of-text")               break;
        case 'D': DEBUG_KEYCODE(0x04, "end-of-transmission")       break;
        case 'E': DEBUG_KEYCODE(0x05, "enquiry")                   break;
        case 'F': DEBUG_KEYCODE(0x06, "acknowledgement")           break;
        case 'G': DEBUG_KEYCODE(0x07, "bell")                      break;
        case 'H': DEBUG_KEYCODE(0x08, "backspace")                 break;
        case 'I': DEBUG_KEYCODE(0x09, "hrizontal-tab")             break;
        case 'J': DEBUG_KEYCODE(0x0A, "line-feed")                 break;
        case 'K': DEBUG_KEYCODE(0x0B, "vertical-tab")              break;
        case 'L': DEBUG_KEYCODE(0x0C, "form-feed")                 break;
        case 'M': DEBUG_KEYCODE(0x0D, "carriage-return")           break;
        case 'N': DEBUG_KEYCODE(0x0E, "shift-out")                 break;
        case 'O': DEBUG_KEYCODE(0x0F, "shift-in")                  break;
        case 'P': DEBUG_KEYCODE(0x10, "data-link-escape")          break;
        case 'Q': DEBUG_KEYCODE(0x11, "device-control-1")          break;
        case 'R': DEBUG_KEYCODE(0x12, "device-control-2")          break;
        case 'S': DEBUG_KEYCODE(0x13, "device-control-3")          break;
        case 'T': DEBUG_KEYCODE(0x14, "device-control-4")          break;
        case 'U': DEBUG_KEYCODE(0x15, "negative-acknowledgement")  break;
        case 'V': DEBUG_KEYCODE(0x16, "synchronous-idle")          break;
        case 'W': DEBUG_KEYCODE(0x17, "end-of-transmission-block") break;
        case 'X': DEBUG_KEYCODE(0x18, "cancel")                    break;
        case 'Y': DEBUG_KEYCODE(0x19, "end-of-medium")             break;
        case 'Z': DEBUG_KEYCODE(0x1A, "substitute")                break;
        case '[': DEBUG_KEYCODE(0x1B, "escape")                    break;
        case '\\': DEBUG_KEYCODE(0x1C, "file-separator")           break;
        case ']': DEBUG_KEYCODE(0x1D, "group-separator")           break;
        case '^': DEBUG_KEYCODE(0x1E, "record-separator")          break;
        case '_': DEBUG_KEYCODE(0x1F, "unit-separator")            break;
        case '?': DEBUG_KEYCODE(0x7F, "delete")                    break;
        case '2': DEBUG_KEYCODE(0x00, "null")                      break;
        case '3': DEBUG_KEYCODE(0x1B, "escape")                    break;
        case '4': DEBUG_KEYCODE(0x1C, "file-separator")            break;
        case '5': DEBUG_KEYCODE(0x1D, "group-separator")           break;
        case '6': DEBUG_KEYCODE(0x1E, "record-separator")          break;
        case '7': DEBUG_KEYCODE(0x1F, "unit-separator")            break;
        case '8': DEBUG_KEYCODE(0x7F, "delete")                    break;
        }
        break;

      default:
        if (0x1F < code && code < 0x7F)
        {
          std::cerr << "; keyboard\t; ascii " << code << std::endl;
          return true;
        }
      }

      return false;
    }
  };
} // namespace xcb

namespace cairo
{
  constexpr std::uint32_t event_mask
  {
    XCB_EVENT_MASK_NO_EVENT              * 1 |
    XCB_EVENT_MASK_KEY_PRESS             * 1 |
    XCB_EVENT_MASK_KEY_RELEASE           * 0 |
    XCB_EVENT_MASK_BUTTON_PRESS          * 0 |
    XCB_EVENT_MASK_BUTTON_RELEASE        * 0 |
    XCB_EVENT_MASK_ENTER_WINDOW          * 0 |
    XCB_EVENT_MASK_LEAVE_WINDOW          * 0 |
    XCB_EVENT_MASK_POINTER_MOTION        * 0 |
    XCB_EVENT_MASK_POINTER_MOTION_HINT   * 0 |
    XCB_EVENT_MASK_BUTTON_1_MOTION       * 0 |
    XCB_EVENT_MASK_BUTTON_2_MOTION       * 0 |
    XCB_EVENT_MASK_BUTTON_3_MOTION       * 0 |
    XCB_EVENT_MASK_BUTTON_4_MOTION       * 0 |
    XCB_EVENT_MASK_BUTTON_5_MOTION       * 0 |
    XCB_EVENT_MASK_BUTTON_MOTION         * 0 |
    XCB_EVENT_MASK_KEYMAP_STATE          * 0 |
    XCB_EVENT_MASK_EXPOSURE              * 1 |
    XCB_EVENT_MASK_VISIBILITY_CHANGE     * 0 |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY      * 1 |
    XCB_EVENT_MASK_RESIZE_REDIRECT       * 0 |
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY   * 0 |
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT * 0 |
    XCB_EVENT_MASK_FOCUS_CHANGE          * 0 |
    XCB_EVENT_MASK_PROPERTY_CHANGE       * 0 |
    XCB_EVENT_MASK_COLOR_MAP_CHANGE      * 0 |
    XCB_EVENT_MASK_OWNER_GRAB_BUTTON     * 0
  };

  struct surface
    : public xcb::machine<surface, event_mask>
    , public std::shared_ptr<cairo_surface_t>
  {
    explicit surface()
      : machine<surface, event_mask> {}
      , std::shared_ptr<cairo_surface_t> {
          cairo_xcb_surface_create(connection, value, root_visual(connection), 1, 1),
          cairo_surface_destroy
        }
    {
      std::cerr << "; surface\t; instatiated" << std::endl;
    }

    // explicit surface(const surface& parent)
    //   : machine<surface, event_mask> {parent.value}
    //   , std::shared_ptr<cairo_surface_t> {cairo_xcb_surface_create(
    //       connection, value, root_visual(connection), 1, 1
    //     ), cairo_surface_destroy}
    // {}

    operator cairo_surface_t*() const noexcept
    {
      return get();
    }

    decltype(auto) flush()
    {
      cairo_surface_flush(*this);
      std::cerr << "; surface\t; flushed" << std::endl;
      connection.flush();
    }

    decltype(auto) size(const std::uint32_t width, const std::uint32_t height) const noexcept
    {
      return cairo_xcb_surface_set_size(*this, width, height);
    }

    // void operator()(const std::unique_ptr<xcb_expose_event_t> event)
    // {
    // }

    void operator()(std::unique_ptr<xcb_key_press_event_t>&& event)
    {
      static xcb::keyboard<char> keyboard {connection};
      keyboard.press(std::move(event));
    }

    void operator()(const std::unique_ptr<xcb_configure_notify_event_t> event)
    {
      size(event->width, event->height);
    }
  };
} // namespace cairo

// struct cursor
// {
//   std::size_t row, column;
// };
//
// struct screen
// {
// };

std::size_t row {24}, column {80};

const auto* font {"Monospace:pixelsize=14:antialias=true:autohint=true"};
const auto* name {"vt10x-256color"};

int main(const int argc, char const* const* const argv)
{
  const std::vector<std::string> args {argv + 1, argv + argc};

  cairo::surface main {};

  main.configure(XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, 1280u, 720u);
  main.size(1280, 720);

  main.map();
  main.flush();

  main.execute();

  return 0;
}

