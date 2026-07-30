/**
 *
 * @file http_api_query_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <vix/websocket/HttpApi.hpp>

namespace
{
  namespace detail =
      vix::websocket::http::detail;

  struct TargetRequest
  {
    std::string targetValue{"/"};

    [[nodiscard]]
    std::string_view target() const noexcept
    {
      return std::string_view{
          targetValue.data(),
          targetValue.size()};
    }
  };

  struct QueryValueRequest
  {
    std::unordered_map<
        std::string,
        std::string>
        values{};

    std::string fallbackTarget{"/"};

    [[nodiscard]]
    std::string query_value(
        std::string_view key) const
    {
      const auto it =
          values.find(
              std::string{key});

      if (it == values.end())
      {
        return {};
      }

      return it->second;
    }

    [[nodiscard]]
    std::string_view target() const noexcept
    {
      return fallbackTarget;
    }
  };

  static void test_return_types()
  {
    static_assert(
        std::is_same_v<
            decltype(detail::hex_val('0')),
            int>);

    static_assert(
        std::is_same_v<
            decltype(detail::url_decode(
                std::declval<
                    std::string_view>())),
            std::string>);

    static_assert(
        std::is_same_v<
            decltype(detail::query_param_from_target(
                std::declval<
                    std::string_view>(),
                std::declval<
                    std::string_view>())),
            std::optional<std::string>>);

    static_assert(
        std::is_same_v<
            decltype(detail::get_query_param(
                std::declval<
                    const TargetRequest &>(),
                std::declval<
                    std::string_view>())),
            std::optional<std::string>>);

    static_assert(
        std::is_same_v<
            decltype(detail::has_query_param(
                std::declval<
                    const TargetRequest &>(),
                std::declval<
                    std::string_view>())),
            bool>);
  }

  static void test_hex_digits()
  {
    for (int value = 0;
         value <= 9;
         ++value)
    {
      const char character =
          static_cast<char>(
              '0' + value);

      assert(
          detail::hex_val(character) ==
          value);
    }
  }

  static void test_lowercase_hex_letters()
  {
    assert(detail::hex_val('a') == 10);
    assert(detail::hex_val('b') == 11);
    assert(detail::hex_val('c') == 12);
    assert(detail::hex_val('d') == 13);
    assert(detail::hex_val('e') == 14);
    assert(detail::hex_val('f') == 15);
  }

  static void test_uppercase_hex_letters()
  {
    assert(detail::hex_val('A') == 10);
    assert(detail::hex_val('B') == 11);
    assert(detail::hex_val('C') == 12);
    assert(detail::hex_val('D') == 13);
    assert(detail::hex_val('E') == 14);
    assert(detail::hex_val('F') == 15);
  }

  static void test_invalid_hex_characters()
  {
    assert(detail::hex_val('g') == -1);
    assert(detail::hex_val('G') == -1);
    assert(detail::hex_val('-') == -1);
    assert(detail::hex_val(' ') == -1);
    assert(detail::hex_val('\0') == -1);
  }

  static void test_url_decode_empty_value()
  {
    assert(
        detail::url_decode("").empty());
  }

  static void test_url_decode_plain_value()
  {
    assert(
        detail::url_decode(
            "session-42") ==
        "session-42");
  }

  static void test_url_decode_plus_as_space()
  {
    assert(
        detail::url_decode(
            "hello+from+vix") ==
        "hello from vix");
  }

  static void test_url_decode_percent_sequences()
  {
    assert(
        detail::url_decode(
            "room%3Ageneral") ==
        "room:general");

    assert(
        detail::url_decode(
            "hello%20world") ==
        "hello world");

    assert(
        detail::url_decode(
            "%2Fws%2Fpoll") ==
        "/ws/poll");

    assert(
        detail::url_decode(
            "%7Euser") ==
        "~user");
  }

  static void test_url_decode_lowercase_hex()
  {
    assert(
        detail::url_decode(
            "%2fws%2fpoll") ==
        "/ws/poll");
  }

  static void test_url_decode_mixed_value()
  {
    assert(
        detail::url_decode(
            "room%3Ageneral+chat%2Fmain") ==
        "room:general chat/main");
  }

  static void test_url_decode_invalid_percent_sequence()
  {
    assert(
        detail::url_decode(
            "value%GGtest") ==
        "value%GGtest");

    assert(
        detail::url_decode(
            "value%2Xtest") ==
        "value%2Xtest");
  }

  static void test_url_decode_incomplete_percent_sequence()
  {
    assert(
        detail::url_decode(
            "value%") ==
        "value%");

    assert(
        detail::url_decode(
            "value%2") ==
        "value%2");
  }

  static void test_url_decode_embedded_null()
  {
    const std::string decoded =
        detail::url_decode("%00");

    assert(decoded.size() == 1u);
    assert(decoded[0] == '\0');
  }

  static void test_query_without_question_mark()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll",
            "session_id");

    assert(!value.has_value());
  }

  static void test_empty_query_string()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?",
            "session_id");

    assert(!value.has_value());
  }

  static void test_first_query_parameter()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=session-1&max=10",
            "session_id");

    assert(value.has_value());
    assert(*value == "session-1");
  }

  static void test_middle_query_parameter()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?first=1&session_id=session-2&max=10",
            "session_id");

    assert(value.has_value());
    assert(*value == "session-2");
  }

  static void test_last_query_parameter()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=session-3&max=25",
            "max");

    assert(value.has_value());
    assert(*value == "25");
  }

  static void test_missing_query_parameter()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=session-1",
            "max");

    assert(!value.has_value());
  }

  static void test_parameter_without_equals()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?create&max=10",
            "create");

    assert(value.has_value());
    assert(value->empty());
  }

  static void test_parameter_with_empty_value()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=&max=10",
            "session_id");

    assert(value.has_value());
    assert(value->empty());
  }

  static void test_query_value_is_url_decoded()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=room%3Ageneral+chat",
            "session_id");

    assert(value.has_value());
    assert(*value == "room:general chat");
  }

  static void test_duplicate_parameter_returns_first_value()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=first&session_id=second",
            "session_id");

    assert(value.has_value());
    assert(*value == "first");
  }

  static void test_value_can_contain_equals()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?token=a=b=c&max=10",
            "token");

    assert(value.has_value());
    assert(*value == "a=b=c");
  }

  static void test_fragment_remains_part_of_value()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=session-1#fragment",
            "session_id");

    assert(value.has_value());
    assert(*value == "session-1#fragment");
  }

  static void test_parameter_names_are_case_sensitive()
  {
    const auto lower =
        detail::query_param_from_target(
            "/ws/poll?Session_Id=session-1",
            "session_id");

    const auto exact =
        detail::query_param_from_target(
            "/ws/poll?Session_Id=session-1",
            "Session_Id");

    assert(!lower.has_value());

    assert(exact.has_value());
    assert(*exact == "session-1");
  }

  static void test_parameter_names_are_not_url_decoded()
  {
    const auto decodedName =
        detail::query_param_from_target(
            "/ws/poll?session%5Fid=session-1",
            "session_id");

    const auto encodedName =
        detail::query_param_from_target(
            "/ws/poll?session%5Fid=session-1",
            "session%5Fid");

    assert(!decodedName.has_value());

    assert(encodedName.has_value());
    assert(*encodedName == "session-1");
  }

  static void test_semicolon_is_not_a_separator()
  {
    const auto value =
        detail::query_param_from_target(
            "/ws/poll?session_id=first;max=10",
            "session_id");

    assert(value.has_value());
    assert(*value == "first;max=10");
  }

  static void test_request_target_string()
  {
    const TargetRequest request{
        "/ws/poll?session_id=session-42"};

    assert(
        detail::request_target_string(
            request) ==
        "/ws/poll?session_id=session-42");
  }

  static void test_request_target_preserves_embedded_null()
  {
    TargetRequest request;

    request.targetValue =
        std::string{
            'a',
            '\0',
            'b'};

    const std::string target =
        detail::request_target_string(
            request);

    assert(target.size() == 3u);
    assert(target[0] == 'a');
    assert(target[1] == '\0');
    assert(target[2] == 'b');
  }

  static void test_get_query_param_from_target()
  {
    const TargetRequest request{
        "/ws/poll?session_id=session-42&max=5"};

    const auto session =
        detail::get_query_param(
            request,
            "session_id");

    const auto maximum =
        detail::get_query_param(
            request,
            "max");

    assert(session.has_value());
    assert(*session == "session-42");

    assert(maximum.has_value());
    assert(*maximum == "5");
  }

  static void test_get_query_param_decodes_target_value()
  {
    const TargetRequest request{
        "/ws/poll?session_id=room%3Ageneral"};

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(value.has_value());
    assert(*value == "room:general");
  }

  static void test_get_query_param_preserves_empty_target_value()
  {
    const TargetRequest request{
        "/ws/poll?session_id="};

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(value.has_value());
    assert(value->empty());
  }

  static void test_get_query_param_uses_query_value_when_available()
  {
    QueryValueRequest request;

    request.values.emplace(
        "session_id",
        "session-from-query-value");

    request.fallbackTarget =
        "/ws/poll?session_id=session-from-target";

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(value.has_value());

    assert(
        *value ==
        "session-from-query-value");
  }

  static void test_query_value_result_is_not_url_decoded()
  {
    QueryValueRequest request;

    request.values.emplace(
        "session_id",
        "room%3Ageneral");

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(value.has_value());

    assert(
        *value ==
        "room%3Ageneral");
  }

  static void test_empty_query_value_returns_nullopt()
  {
    QueryValueRequest request;

    request.values.emplace(
        "session_id",
        "");

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(!value.has_value());
  }

  static void test_query_value_does_not_fallback_to_target()
  {
    QueryValueRequest request;

    request.fallbackTarget =
        "/ws/poll?session_id=session-from-target";

    const auto value =
        detail::get_query_param(
            request,
            "session_id");

    assert(!value.has_value());
  }

  static void test_has_query_param_for_present_value()
  {
    const TargetRequest request{
        "/ws/poll?session_id=session-42"};

    assert(
        detail::has_query_param(
            request,
            "session_id") == true);
  }

  static void test_has_query_param_for_missing_value()
  {
    const TargetRequest request{
        "/ws/poll?session_id=session-42"};

    assert(
        detail::has_query_param(
            request,
            "max") == false);
  }

  static void test_has_query_param_for_empty_target_value()
  {
    const TargetRequest request{
        "/ws/poll?session_id="};

    assert(
        detail::has_query_param(
            request,
            "session_id") == true);
  }

  static void test_has_query_param_for_flag_parameter()
  {
    const TargetRequest request{
        "/ws/poll?create"};

    assert(
        detail::has_query_param(
            request,
            "create") == true);
  }

  static void test_has_query_param_for_empty_query_value()
  {
    QueryValueRequest request;

    request.values.emplace(
        "create",
        "");

    assert(
        detail::has_query_param(
            request,
            "create") == false);
  }

} // namespace

int main()
{
  test_return_types();

  test_hex_digits();
  test_lowercase_hex_letters();
  test_uppercase_hex_letters();
  test_invalid_hex_characters();

  test_url_decode_empty_value();
  test_url_decode_plain_value();
  test_url_decode_plus_as_space();
  test_url_decode_percent_sequences();
  test_url_decode_lowercase_hex();
  test_url_decode_mixed_value();
  test_url_decode_invalid_percent_sequence();
  test_url_decode_incomplete_percent_sequence();
  test_url_decode_embedded_null();

  test_query_without_question_mark();
  test_empty_query_string();

  test_first_query_parameter();
  test_middle_query_parameter();
  test_last_query_parameter();
  test_missing_query_parameter();

  test_parameter_without_equals();
  test_parameter_with_empty_value();

  test_query_value_is_url_decoded();
  test_duplicate_parameter_returns_first_value();
  test_value_can_contain_equals();
  test_fragment_remains_part_of_value();

  test_parameter_names_are_case_sensitive();
  test_parameter_names_are_not_url_decoded();
  test_semicolon_is_not_a_separator();

  test_request_target_string();
  test_request_target_preserves_embedded_null();

  test_get_query_param_from_target();
  test_get_query_param_decodes_target_value();
  test_get_query_param_preserves_empty_target_value();

  test_get_query_param_uses_query_value_when_available();
  test_query_value_result_is_not_url_decoded();
  test_empty_query_value_returns_nullopt();
  test_query_value_does_not_fallback_to_target();

  test_has_query_param_for_present_value();
  test_has_query_param_for_missing_value();
  test_has_query_param_for_empty_target_value();
  test_has_query_param_for_flag_parameter();
  test_has_query_param_for_empty_query_value();

  return 0;
}
