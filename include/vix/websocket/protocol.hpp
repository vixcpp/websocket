/**
 *
 *  @file protocol.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_WEBSOCKET_PROTOCOL_HPP
#define VIX_WEBSOCKET_PROTOCOL_HPP

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp>

namespace vix::websocket
{
  namespace detail
  {
    enum class Opcode : std::uint8_t
    {
      Continuation = 0x0,
      Text = 0x1,
      Binary = 0x2,
      Close = 0x8,
      Ping = 0x9,
      Pong = 0xA,
    };

    struct Frame
    {
      bool fin{true};
      Opcode opcode{Opcode::Text};
      bool masked{false};
      std::array<std::byte, 4> mask_key{};
      std::vector<std::byte> payload{};

      std::string text() const
      {
        return std::string(
            reinterpret_cast<const char *>(payload.data()),
            payload.size());
      }
    };

    struct FrameHeader
    {
      bool fin{true};
      Opcode opcode{Opcode::Text};
      bool masked{false};
      std::array<std::byte, 4> mask_key{};
      std::size_t payload_length{0};
      std::size_t header_size{0};
    };

    inline std::uint8_t to_u8(std::byte b) noexcept
    {
      return std::to_integer<std::uint8_t>(b);
    }

    inline std::byte to_byte(std::uint8_t v) noexcept
    {
      return static_cast<std::byte>(v);
    }

    inline void append_u16_be(std::vector<std::byte> &out, std::uint16_t v)
    {
      out.push_back(to_byte(static_cast<std::uint8_t>((v >> 8) & 0xFF)));
      out.push_back(to_byte(static_cast<std::uint8_t>(v & 0xFF)));
    }

    inline void append_u64_be(std::vector<std::byte> &out, std::uint64_t v)
    {
      for (int i = 7; i >= 0; --i)
      {
        out.push_back(to_byte(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF)));
      }
    }

    inline std::uint16_t read_u16_be(const std::byte *p)
    {
      return static_cast<std::uint16_t>(
          (static_cast<std::uint16_t>(to_u8(p[0])) << 8) |
          static_cast<std::uint16_t>(to_u8(p[1])));
    }

    inline std::uint64_t read_u64_be(const std::byte *p)
    {
      std::uint64_t v = 0;
      for (int i = 0; i < 8; ++i)
      {
        v = (v << 8) | static_cast<std::uint64_t>(to_u8(p[i]));
      }
      return v;
    }

    inline std::array<std::byte, 4> random_mask_key()
    {
      static thread_local std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<int> dist(0, 255);

      return {
          to_byte(static_cast<std::uint8_t>(dist(rng))),
          to_byte(static_cast<std::uint8_t>(dist(rng))),
          to_byte(static_cast<std::uint8_t>(dist(rng))),
          to_byte(static_cast<std::uint8_t>(dist(rng)))};
    }

    inline void apply_mask_in_place(
        std::vector<std::byte> &payload,
        const std::array<std::byte, 4> &mask_key)
    {
      for (std::size_t i = 0; i < payload.size(); ++i)
      {
        payload[i] = payload[i] ^ mask_key[i % 4];
      }
    }

    inline std::string trim_copy(std::string s)
    {
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
      {
        s.erase(s.begin());
      }

      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
      {
        s.pop_back();
      }

      return s;
    }

    inline std::string to_lower_copy(std::string s)
    {
      for (char &ch : s)
      {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      return s;
    }

    inline std::string get_header_value(
        const std::string &raw_http,
        const std::string &header_name)
    {
      const std::string needle = to_lower_copy(header_name) + ":";

      std::size_t pos = 0;
      while (pos < raw_http.size())
      {
        const std::size_t line_end = raw_http.find("\r\n", pos);
        const std::size_t end = (line_end == std::string::npos) ? raw_http.size() : line_end;
        std::string line = raw_http.substr(pos, end - pos);
        std::string lower = to_lower_copy(line);

        if (lower.rfind(needle, 0) == 0)
        {
          return trim_copy(line.substr(needle.size()));
        }

        if (line_end == std::string::npos)
        {
          break;
        }

        pos = line_end + 2;
      }

      return {};
    }

    inline bool header_contains_token(
        const std::string &raw_http,
        const std::string &header_name,
        const std::string &token)
    {
      const std::string value = to_lower_copy(get_header_value(raw_http, header_name));
      const std::string wanted = to_lower_copy(token);

      if (value.empty())
      {
        return false;
      }

      std::size_t start = 0;
      while (start < value.size())
      {
        std::size_t comma = value.find(',', start);
        std::string part = (comma == std::string::npos)
                               ? value.substr(start)
                               : value.substr(start, comma - start);

        part = trim_copy(part);
        if (part == wanted)
        {
          return true;
        }

        if (comma == std::string::npos)
        {
          break;
        }
        start = comma + 1;
      }

      return false;
    }

    inline std::uint32_t rol32(std::uint32_t value, std::uint32_t bits) noexcept
    {
      return (value << bits) | (value >> (32u - bits));
    }

    inline std::array<unsigned char, 20> sha1_bytes(const unsigned char *data, std::size_t len)
    {
      std::uint32_t h0 = 0x67452301u;
      std::uint32_t h1 = 0xEFCDAB89u;
      std::uint32_t h2 = 0x98BADCFEu;
      std::uint32_t h3 = 0x10325476u;
      std::uint32_t h4 = 0xC3D2E1F0u;

      const std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8ull;

      std::vector<unsigned char> msg(data, data + len);
      msg.push_back(0x80u);

      while ((msg.size() % 64u) != 56u)
      {
        msg.push_back(0x00u);
      }

      for (int i = 7; i >= 0; --i)
      {
        msg.push_back(static_cast<unsigned char>((bit_len >> (i * 8)) & 0xFFu));
      }

      for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64)
      {
        std::uint32_t w[80]{};

        for (int i = 0; i < 16; ++i)
        {
          const std::size_t j = chunk + static_cast<std::size_t>(i) * 4u;
          w[i] = (static_cast<std::uint32_t>(msg[j]) << 24) |
                 (static_cast<std::uint32_t>(msg[j + 1]) << 16) |
                 (static_cast<std::uint32_t>(msg[j + 2]) << 8) |
                 static_cast<std::uint32_t>(msg[j + 3]);
        }

        for (int i = 16; i < 80; ++i)
        {
          w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (int i = 0; i < 80; ++i)
        {
          std::uint32_t f = 0;
          std::uint32_t k = 0;

          if (i < 20)
          {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
          }
          else if (i < 40)
          {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
          }
          else if (i < 60)
          {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
          }
          else
          {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
          }

          const std::uint32_t temp = rol32(a, 5) + f + e + k + w[i];
          e = d;
          d = c;
          c = rol32(b, 30);
          b = a;
          a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
      }

      std::array<unsigned char, 20> out{};
      const std::uint32_t hs[5] = {h0, h1, h2, h3, h4};

      for (std::size_t i = 0; i < 5; ++i)
      {
        out[i * 4 + 0] = static_cast<unsigned char>((hs[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = static_cast<unsigned char>((hs[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = static_cast<unsigned char>((hs[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = static_cast<unsigned char>(hs[i] & 0xFFu);
      }

      return out;
    }

    inline std::string base64_encode(const unsigned char *data, std::size_t len)
    {
      static constexpr char table[] =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

      std::string out;
      out.reserve(((len + 2u) / 3u) * 4u);

      std::size_t i = 0;
      while (i + 3u <= len)
      {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8) |
            static_cast<std::uint32_t>(data[i + 2]);

        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);

        i += 3u;
      }

      const std::size_t rem = len - i;
      if (rem == 1u)
      {
        const std::uint32_t n =
            static_cast<std::uint32_t>(data[i]) << 16;

        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
      }
      else if (rem == 2u)
      {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8);

        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back('=');
      }

      return out;
    }

    inline std::string websocket_accept_from_key(const std::string &client_key)
    {
      static constexpr const char *GUID =
          "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

      const std::string input = client_key + GUID;
      const auto digest = sha1_bytes(
          reinterpret_cast<const unsigned char *>(input.data()),
          input.size());

      return base64_encode(digest.data(), digest.size());
    }

    inline std::string generate_websocket_key()
    {
      std::array<unsigned char, 16> bytes{};
      static thread_local std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<int> dist(0, 255);

      for (auto &b : bytes)
      {
        b = static_cast<unsigned char>(dist(rng));
      }

      return base64_encode(bytes.data(), bytes.size());
    }

    inline void validate_handshake_response(
        const std::string &raw_response,
        const std::string &handshake_key)
    {
      const std::size_t status_end = raw_response.find("\r\n");
      const std::string status_line =
          status_end == std::string::npos
              ? raw_response
              : raw_response.substr(0, status_end);

      if (status_line.find("101") == std::string::npos)
      {
        throw std::runtime_error("invalid websocket handshake: expected HTTP 101");
      }

      if (!header_contains_token(raw_response, "Connection", "Upgrade"))
      {
        throw std::runtime_error("invalid websocket handshake: missing Connection: Upgrade");
      }

      const std::string upgrade = to_lower_copy(get_header_value(raw_response, "Upgrade"));
      if (upgrade != "websocket")
      {
        throw std::runtime_error("invalid websocket handshake: missing Upgrade: websocket");
      }

      const std::string accept = trim_copy(get_header_value(raw_response, "Sec-WebSocket-Accept"));
      const std::string expected = websocket_accept_from_key(handshake_key);

      if (accept != expected)
      {
        throw std::runtime_error("invalid websocket handshake: bad Sec-WebSocket-Accept");
      }
    }

    inline std::vector<std::byte> build_frame(
        Opcode opcode,
        const std::vector<std::byte> &payload,
        bool fin,
        bool masked)
    {
      std::vector<std::byte> out;
      out.reserve(14 + payload.size());

      const std::uint8_t b0 =
          static_cast<std::uint8_t>((fin ? 0x80 : 0x00) |
                                    (static_cast<std::uint8_t>(opcode) & 0x0F));
      out.push_back(to_byte(b0));

      const std::size_t len = payload.size();
      std::uint8_t b1 = masked ? 0x80 : 0x00;

      if (len <= 125)
      {
        b1 |= static_cast<std::uint8_t>(len);
        out.push_back(to_byte(b1));
      }
      else if (len <= 0xFFFF)
      {
        b1 |= 126;
        out.push_back(to_byte(b1));
        append_u16_be(out, static_cast<std::uint16_t>(len));
      }
      else
      {
        b1 |= 127;
        out.push_back(to_byte(b1));
        append_u64_be(out, static_cast<std::uint64_t>(len));
      }

      std::vector<std::byte> body = payload;

      if (masked)
      {
        const auto key = random_mask_key();
        out.insert(out.end(), key.begin(), key.end());
        apply_mask_in_place(body, key);
      }

      out.insert(out.end(), body.begin(), body.end());
      return out;
    }

    inline std::vector<std::byte> build_text_frame(std::string_view text, bool masked)
    {
      std::vector<std::byte> payload;
      payload.reserve(text.size());

      for (char ch : text)
      {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
      }

      return build_frame(Opcode::Text, payload, true, masked);
    }

    inline std::vector<std::byte> build_ping_frame(bool masked)
    {
      return build_frame(Opcode::Ping, {}, true, masked);
    }

    inline std::vector<std::byte> build_close_frame(bool masked)
    {
      return build_frame(Opcode::Close, {}, true, masked);
    }

    inline std::vector<std::byte> build_pong_frame(
        const std::vector<std::byte> &payload,
        bool masked)
    {
      return build_frame(Opcode::Pong, payload, true, masked);
    }

    inline FrameHeader parse_frame_header(const std::byte *data, std::size_t size)
    {
      if (size < 2)
      {
        throw std::runtime_error("websocket frame too short");
      }

      const std::uint8_t b0 = to_u8(data[0]);
      const std::uint8_t b1 = to_u8(data[1]);

      FrameHeader h;
      h.fin = (b0 & 0x80) != 0;
      h.opcode = static_cast<Opcode>(b0 & 0x0F);
      h.masked = (b1 & 0x80) != 0;

      const std::uint8_t len7 = static_cast<std::uint8_t>(b1 & 0x7F);
      std::size_t pos = 2;

      if (len7 <= 125)
      {
        h.payload_length = len7;
      }
      else if (len7 == 126)
      {
        if (size < pos + 2)
        {
          throw std::runtime_error("incomplete websocket extended length (16-bit)");
        }

        h.payload_length = read_u16_be(data + pos);
        pos += 2;
      }
      else
      {
        if (size < pos + 8)
        {
          throw std::runtime_error("incomplete websocket extended length (64-bit)");
        }

        const std::uint64_t len64 = read_u64_be(data + pos);
        pos += 8;

        if (len64 > static_cast<std::uint64_t>(static_cast<std::size_t>(-1)))
        {
          throw std::runtime_error("websocket payload length overflow");
        }

        h.payload_length = static_cast<std::size_t>(len64);
      }

      if (h.masked)
      {
        if (size < pos + 4)
        {
          throw std::runtime_error("incomplete websocket mask key");
        }

        for (std::size_t i = 0; i < 4; ++i)
        {
          h.mask_key[i] = data[pos + i];
        }
        pos += 4;
      }

      h.header_size = pos;
      return h;
    }

    inline Frame decode_frame(const std::vector<std::byte> &bytes)
    {
      if (bytes.size() < 2)
      {
        throw std::runtime_error("websocket frame too short");
      }

      const FrameHeader h = parse_frame_header(bytes.data(), bytes.size());

      if (bytes.size() < h.header_size + h.payload_length)
      {
        throw std::runtime_error("incomplete websocket frame payload");
      }

      Frame f;
      f.fin = h.fin;
      f.opcode = h.opcode;
      f.masked = h.masked;
      f.mask_key = h.mask_key;
      f.payload.resize(h.payload_length);

      for (std::size_t i = 0; i < h.payload_length; ++i)
      {
        f.payload[i] = bytes[h.header_size + i];
      }

      if (f.masked)
      {
        apply_mask_in_place(f.payload, f.mask_key);
      }

      return f;
    }

    inline nlohmann::json ws_token_to_nlohmann(const vix::json::token &t)
    {
      nlohmann::json j = nullptr;
      std::visit(
          [&](auto &&val)
          {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>)
            {
              j = nullptr;
            }
            else if constexpr (std::is_same_v<T, bool> ||
                               std::is_same_v<T, long long> ||
                               std::is_same_v<T, double> ||
                               std::is_same_v<T, std::string>)
            {
              j = val;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::array_t>>)
            {
              if (!val)
              {
                j = nullptr;
                return;
              }

              j = nlohmann::json::array();
              for (const auto &el : val->elems)
              {
                j.push_back(ws_token_to_nlohmann(el));
              }
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::kvs>>)
            {
              if (!val)
              {
                j = nullptr;
                return;
              }

              nlohmann::json obj = nlohmann::json::object();
              const auto &a = val->flat;
              const size_t n = a.size() - (a.size() % 2);

              for (size_t i = 0; i < n; i += 2)
              {
                const auto &k = a[i].v;
                const auto &vv = a[i + 1];

                if (!std::holds_alternative<std::string>(k))
                  continue;

                const auto &key = std::get<std::string>(k);
                obj[key] = ws_token_to_nlohmann(vv);
              }

              j = std::move(obj);
            }
            else
            {
              j = nullptr;
            }
          },
          t.v);

      return j;
    }

    inline nlohmann::json ws_kvs_to_nlohmann(const vix::json::kvs &list)
    {
      nlohmann::json obj = nlohmann::json::object();
      const auto &a = list.flat;
      const size_t n = a.size() - (a.size() % 2);

      for (size_t i = 0; i < n; i += 2)
      {
        const auto &k = a[i].v;
        const auto &v = a[i + 1];

        if (!std::holds_alternative<std::string>(k))
          continue;

        const std::string &key = std::get<std::string>(k);
        obj[key] = ws_token_to_nlohmann(v);
      }

      return obj;
    }

    inline vix::json::kvs nlohmann_payload_to_kvs(const nlohmann::json &payload)
    {
      vix::json::kvs kv;

      if (!payload.is_object())
        return kv;

      for (auto it = payload.begin(); it != payload.end(); ++it)
      {
        const std::string key = it.key();
        const nlohmann::json &val = *it;

        kv.flat.emplace_back(vix::json::token{key});

        if (val.is_string())
        {
          kv.flat.emplace_back(vix::json::token{val.get<std::string>()});
        }
        else if (val.is_boolean())
        {
          kv.flat.emplace_back(vix::json::token{val.get<bool>()});
        }
        else if (val.is_number_integer())
        {
          kv.flat.emplace_back(vix::json::token{val.get<long long>()});
        }
        else if (val.is_number_float())
        {
          kv.flat.emplace_back(vix::json::token{val.get<double>()});
        }
        else if (val.is_null())
        {
          kv.flat.emplace_back(vix::json::token{});
        }
        else
        {
          kv.flat.emplace_back(vix::json::token{});
        }
      }

      return kv;
    }

  } // namespace detail

  struct JsonMessage
  {
    std::string id{};
    std::string kind{"event"};
    std::string ts{};
    std::string room{};
    std::string type{};
    vix::json::kvs payload{};

    JsonMessage() = default;

    std::string get_string(const std::string &key) const
    {
      const auto &a = payload.flat;
      const size_t n = a.size() - (a.size() % 2);

      for (size_t i = 0; i < n; i += 2)
      {
        const auto &k = a[i].v;
        const auto &v = a[i + 1].v;

        if (std::holds_alternative<std::string>(k) &&
            std::get<std::string>(k) == key)
        {
          if (std::holds_alternative<std::string>(v))
            return std::get<std::string>(v);
          return {};
        }
      }

      return {};
    }

    template <typename T>
    std::optional<T> get(const std::string &key) const
    {
      const auto &a = payload.flat;
      const size_t n = a.size() - (a.size() % 2);

      for (size_t i = 0; i < n; i += 2)
      {
        const auto &k = a[i].v;
        const auto &v = a[i + 1].v;

        if (std::holds_alternative<std::string>(k) &&
            std::get<std::string>(k) == key)
        {
          if (std::holds_alternative<T>(v))
            return std::get<T>(v);

          return std::nullopt;
        }
      }

      return std::nullopt;
    }

    static std::optional<JsonMessage> parse(std::string_view s)
    {
      try
      {
        auto j = nlohmann::json::parse(s);
        if (!j.is_object())
          return std::nullopt;

        JsonMessage msg;
        msg.id = j.value("id", std::string{});
        msg.kind = j.value("kind", std::string{});
        msg.ts = j.value("ts", std::string{});
        msg.room = j.value("room", std::string{});
        msg.type = j.value("type", std::string{});

        if (j.contains("payload"))
          msg.payload = detail::nlohmann_payload_to_kvs(j["payload"]);

        if (msg.type.empty())
          return std::nullopt;

        if (msg.kind.empty())
          msg.kind = "event";

        return msg;
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    static std::string serialize(const JsonMessage &m)
    {
      nlohmann::json payload_json = detail::ws_kvs_to_nlohmann(m.payload);

      nlohmann::json j = nlohmann::json::object();
      if (!m.id.empty())
        j["id"] = m.id;
      if (!m.kind.empty())
        j["kind"] = m.kind;
      if (!m.ts.empty())
        j["ts"] = m.ts;
      if (!m.room.empty())
        j["room"] = m.room;

      j["type"] = m.type;
      j["payload"] = payload_json;

      return j.dump();
    }

    static std::string serialize(
        const std::string &type,
        const vix::json::kvs &payload_kvs,
        const std::string &room = {},
        const std::string &id = {},
        const std::string &kind = {},
        const std::string &ts = {})
    {
      JsonMessage m;
      m.type = type;
      m.payload = payload_kvs;
      m.room = room;
      m.id = id;
      m.kind = kind;
      m.ts = ts;

      return serialize(m);
    }

    nlohmann::json to_nlohmann() const
    {
      nlohmann::json payload_json = detail::ws_kvs_to_nlohmann(payload);

      nlohmann::json j = nlohmann::json::object();

      if (!id.empty())
        j["id"] = id;
      if (!kind.empty())
        j["kind"] = kind;
      if (!ts.empty())
        j["ts"] = ts;
      if (!room.empty())
        j["room"] = room;

      j["type"] = type;
      j["payload"] = payload_json;

      return j;
    }
  };

  inline nlohmann::json json_message_to_nlohmann(const JsonMessage &m)
  {
    nlohmann::json payload_json = detail::ws_kvs_to_nlohmann(m.payload);

    nlohmann::json j = nlohmann::json::object();
    if (!m.id.empty())
      j["id"] = m.id;
    if (!m.kind.empty())
      j["kind"] = m.kind;
    if (!m.ts.empty())
      j["ts"] = m.ts;
    if (!m.room.empty())
      j["room"] = m.room;

    j["type"] = m.type;
    j["payload"] = std::move(payload_json);

    return j;
  }

  inline nlohmann::json json_messages_to_nlohmann_array(
      const std::vector<JsonMessage> &messages)
  {
    nlohmann::json arr = nlohmann::json::array();

    if (!messages.empty())
    {
      auto &vec = arr.get_ref<nlohmann::json::array_t &>();
      vec.reserve(messages.size());
    }

    for (const auto &msg : messages)
    {
      arr.push_back(msg.to_nlohmann());
    }

    return arr;
  }

  inline std::string serialize_messages_array(
      const std::vector<JsonMessage> &messages)
  {
    return json_messages_to_nlohmann_array(messages).dump();
  }

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_PROTOCOL_HPP
