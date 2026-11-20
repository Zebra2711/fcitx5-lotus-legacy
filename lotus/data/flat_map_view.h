/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file flat_map_view.h
 * @brief Read-only view over a compile-time flat table.
 */

#ifndef _LOTUS_FLAT_MAP_VIEW_H_
#define _LOTUS_FLAT_MAP_VIEW_H_

#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>

namespace lotus::data {

    /**
     * @brief Non-owning map view over an array of key/value pairs.
     *
     * The generated tables are immutable and small, so a flat array avoids the per-node heap
     * allocations of std::unordered_map and keeps the data in read-only pages.
     */
    template <typename Key, typename Value>
    class FlatMapView {
      public:
        using Entry          = std::pair<Key, Value>;
        using const_iterator = const Entry*;

        /** @brief Creates an empty view. */
        constexpr FlatMapView() = default;

        /**
         * @brief Creates a view over a flat entry array.
         * @param entries Entries to expose; the array must outlive the view.
         */
        constexpr FlatMapView(std::span<const Entry> entries) : entries_(entries) {}

        /** @brief Reports whether the view holds no entry. */
        [[nodiscard]] constexpr bool empty() const noexcept {
            return entries_.empty();
        }

        /** @brief Returns the number of entries. */
        [[nodiscard]] constexpr std::size_t size() const noexcept {
            return entries_.size();
        }

        /** @brief Returns an iterator to the first entry. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return entries_.data();
        }

        /** @brief Returns an iterator past the last entry. */
        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return entries_.data() + entries_.size();
        }

        /**
         * @brief Linear lookup by key.
         * @param key Key to find.
         * @return Iterator to the entry, or end() when absent.
         */
        [[nodiscard]] constexpr const_iterator find(const Key& key) const noexcept {
            for (auto it = begin(); it != end(); ++it) {
                if (it->first == key) {
                    return it;
                }
            }
            return end();
        }

        /**
         * @brief Lookup by key, like std::unordered_map::at.
         * @param key Key to find.
         * @return Value of the entry.
         * @throws std::out_of_range When the key is absent.
         */
        [[nodiscard]] const Value& at(const Key& key) const {
            const auto it = find(key);
            if (it == end()) {
                throw std::out_of_range("FlatMapView::at");
            }
            return it->second;
        }

      private:
        std::span<const Entry> entries_;
    };

} // namespace lotus::data

#endif // _LOTUS_FLAT_MAP_VIEW_H_
