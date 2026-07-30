/**
 *
 * @file message_store_test.cpp
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
#include <memory>
#include <type_traits>
#include <utility>

#if __has_include(<vix/websocket/MessageStore.hpp>)
#include <vix/websocket/MessageStore.hpp>
#elif __has_include(<vix/websocket/storage/MessageStore.hpp>)
#include <vix/websocket/storage/MessageStore.hpp>
#else
#error "Vix WebSocket MessageStore header was not found"
#endif

namespace
{
  using MessageStore =
      vix::websocket::MessageStore;

  class MessageStoreProbe
      : public MessageStore
  {
  public:
    ~MessageStoreProbe() override =
        default;
  };

  static void test_message_store_is_an_interface()
  {
    static_assert(
        std::is_class_v<
            MessageStore>);

    static_assert(
        std::is_abstract_v<
            MessageStore>);

    static_assert(
        std::is_polymorphic_v<
            MessageStore>);

    static_assert(
        std::has_virtual_destructor_v<
            MessageStore>);
  }

  static void test_message_store_is_destructible()
  {
    static_assert(
        std::is_destructible_v<
            MessageStore>);

    static_assert(
        std::is_destructible_v<
            MessageStoreProbe>);
  }

  static void test_message_store_cannot_be_instantiated()
  {
    static_assert(
        !std::is_default_constructible_v<
            MessageStore>);

    static_assert(
        !std::is_copy_constructible_v<
            MessageStore>);

    static_assert(
        !std::is_move_constructible_v<
            MessageStore>);

    static_assert(
        !std::is_copy_assignable_v<
            MessageStore>);

    static_assert(
        !std::is_move_assignable_v<
            MessageStore>);
  }

  static void test_derived_store_remains_abstract_without_operations()
  {
    static_assert(
        std::is_base_of_v<
            MessageStore,
            MessageStoreProbe>);

    static_assert(
        std::is_convertible_v<
            MessageStoreProbe *,
            MessageStore *>);

    static_assert(
        std::is_abstract_v<
            MessageStoreProbe>);

    static_assert(
        !std::is_default_constructible_v<
            MessageStoreProbe>);
  }

  static void test_pointer_contracts()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         std::unique_ptr<
                             MessageStore>>()
                         .get()),
            MessageStore *>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         std::shared_ptr<
                             MessageStore>>()
                         .get()),
            MessageStore *>);
  }

  static void test_null_unique_pointer()
  {
    std::unique_ptr<MessageStore> store;

    assert(store == nullptr);
    assert(store.get() == nullptr);
    assert(static_cast<bool>(store) == false);
  }

  static void test_null_shared_pointer()
  {
    std::shared_ptr<MessageStore> store;

    assert(store == nullptr);
    assert(store.get() == nullptr);
    assert(store.use_count() == 0);
    assert(static_cast<bool>(store) == false);
  }

  static void test_raw_pointer_defaults_to_null()
  {
    MessageStore *store = nullptr;

    assert(store == nullptr);
  }

  static void test_const_pointer_defaults_to_null()
  {
    const MessageStore *store = nullptr;

    assert(store == nullptr);
  }

  static void test_unique_pointer_move()
  {
    std::unique_ptr<MessageStore> source;

    std::unique_ptr<MessageStore> destination{
        std::move(source)};

    assert(source == nullptr);
    assert(destination == nullptr);
  }

  static void test_shared_pointer_copy()
  {
    std::shared_ptr<MessageStore> first;

    std::shared_ptr<MessageStore> second{
        first};

    assert(first == nullptr);
    assert(second == nullptr);

    assert(first.use_count() == 0);
    assert(second.use_count() == 0);
  }

  static void test_shared_pointer_move()
  {
    std::shared_ptr<MessageStore> source;

    std::shared_ptr<MessageStore> destination{
        std::move(source)};

    assert(source == nullptr);
    assert(destination == nullptr);

    assert(source.use_count() == 0);
    assert(destination.use_count() == 0);
  }

  static void test_pointer_comparisons()
  {
    std::unique_ptr<MessageStore> uniqueStore;
    std::shared_ptr<MessageStore> sharedStore;

    assert(uniqueStore == nullptr);
    assert(nullptr == uniqueStore);

    assert(sharedStore == nullptr);
    assert(nullptr == sharedStore);
  }

} // namespace

int main()
{
  test_message_store_is_an_interface();
  test_message_store_is_destructible();
  test_message_store_cannot_be_instantiated();

  test_derived_store_remains_abstract_without_operations();
  test_pointer_contracts();

  test_null_unique_pointer();
  test_null_shared_pointer();

  test_raw_pointer_defaults_to_null();
  test_const_pointer_defaults_to_null();

  test_unique_pointer_move();
  test_shared_pointer_copy();
  test_shared_pointer_move();

  test_pointer_comparisons();

  return 0;
}
