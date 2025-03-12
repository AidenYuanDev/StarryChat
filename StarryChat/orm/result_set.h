#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include "types.h"

namespace StarryChat::orm {

class ResultSet {
 public:
  explicit ResultSet(SqlResultSetPtr ResultSet);
  ~ResultSet() = default;

  ResultSet(ResultSet&& other) noexcept = default;

  ResultSet& operator=(ResultSet&& other) noexcept = default;

  ResultSet(const ResultSet&) = delete;
  ResultSet& operator=(const ResultSet&) = delete;

  bool next();

  bool isNull(int columnIndex) const;
  bool isNull(const std::string& columnName) const;

  SqlValue getValue(int columnIndex) const;
  SqlValue getValue(const std::string& columnName) const;

  template <typename T>
  T get(const std::string& columnName) const {
    SqlValue value = getValue(columnName);
    return convertToType<T>(value);
  }

  template <typename T>
  std::optional<T> getOptional(int columnIndex) const {
    if (isNull(columnIndex)) {
      return std::nullopt;
    }
    return get<T>(columnIndex);
  }

  template <typename T>
  std::optional<T> getOptional(const std::string& columnName) const {
    if (isNull(columnName)) {
      return std::nullopt;
    }
    return get<T>(columnName);
  }

  RowData getRow() const;
  std::vector<RowData> getAll();
  int getColumnCount() const;
  std::vector<std::string> getColumnNames() const;
  SqlResultSet* getRawResultSet() const { return resultSet_.get(); }
  void forEach(const RowHandler& callback);

 private:
  SqlResultSetPtr resultSet_;
  std::vector<std::string> columns_;
  int columnCount_ = 0;

  void initColumnInfo();

  template <typename T>
  T convertToType(const SqlValue& value) const {
    return std::visit(
        [](auto&& arg) -> T {
          using U = std::decay_t<decltype(arg)>;

          if constexpr (std::is_same_v<U, std::nullptr_t>) {
            if constexpr (std::is_same_v<T, std::string>) {
              return "";
            } else if constexpr (std::is_arithmetic_v<T>) {
              return T{};
            } else if constexpr (std::is_same_v<T, bool>) {
              return false;
            } else {
              throw std::bad_variant_access();
            }
          } else if constexpr (std::is_same_v<T, U>) {
            return arg;
          } else if constexpr (std::is_arithmetic_v<T> &&
                               std::is_arithmetic_v<U>) {
            return static_cast<T>(arg);
          } else if constexpr (std::is_same_v<T, std::string>) {
            if constexpr (std::is_same_v<U, TimePoint>) {
              return std::format("{:%Y-%m-%d %H:%M:%S}", arg);
            } else if constexpr (std::is_same_v<U, bool>) {
              return arg ? "1" : "0";
            } else {
              return std::to_string(arg);
            }
          } else if constexpr (std::is_same_v<T, bool>) {
            if constexpr (std::is_arithmetic_v<U>) {
              return arg != U{};
            } else if constexpr (std::is_same_v<U, std::string>) {
              return !arg.empty() && arg != "0" && arg != "false" &&
                     arg != "False" && arg != "FALSE";
            } else {
              throw std::bad_variant_access();
            }
          } else {
            throw std::bad_variant_access();
          }
        },
        value);
  }
  // template <>
  // TimePoint convertToType<TimePoint>(const SqlValue& value) const;
};

}  // namespace StarryChat::orm
