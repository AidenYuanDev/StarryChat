#include "logging.h"
#include "result_set.h"
#include "types.h"

#include <chrono>
#include <iomanip>
#include <mariadb/conncpp/ResultSetMetaData.hpp>
#include <sstream>

using namespace StarryChat::orm;

ResultSet::ResultSet(SqlResultSetPtr resultSet)
    : resultSet_(std::move(resultSet)) {
  if (!resultSet_) {
    LOG_ERROR << "Cannot create ResultSet with null SqlResultSet";
    throw std::invalid_argument("ResultSet pointer cannot be null");
  }

  initColumnInfo();
}

void ResultSet::initColumnInfo() {
  try {
    sql::ResultSetMetaData* meta = resultSet_->getMetaData();
    columnCount_ = meta->getColumnCount();
    columns_.reserve(columnCount_);

    for (int i = 1; i <= columnCount_; i++) {
      columns_.push_back(meta->getColumnName(i).c_str());
    }

    LOG_DEBUG << " Initialized result set with " << columnCount_ << " columns";
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to initialize column info: " << ex.what();
    throw;
  }
}

bool ResultSet::next() {
  try {
    return resultSet_->next();
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to move to next row: " << ex.what();
    throw;
  }
}

bool ResultSet::isNull(int columnIndex) const {
  try {
    return resultSet_->isNull(columnIndex + 1);
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to check if column " << columnIndex
              << " is null: " << ex.what();
    throw;
  }
}

bool ResultSet::isNull(const std::string& columnName) const {
  try {
    return resultSet_->isNull(columnName);
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to check if column '" << columnName
              << "' is null: " << ex.what();
    throw;
  }
}

SqlValue ResultSet::getValue(int columnIndex) const {
  try {
    int mariadbIndex = columnIndex + 1;

    if (resultSet_->isNull(mariadbIndex)) {
      return nullptr;
    }

    int typeInt = resultSet_->getMetaData()->getColumnType(mariadbIndex);

    switch (typeInt) {
      case sql::Types::BIT:
      case sql::Types::BOOLEAN:
        return resultSet_->getBoolean(mariadbIndex);

      case sql::Types::TINYINT:
      case sql::Types::SMALLINT:
      case sql::Types::INTEGER:
        return resultSet_->getInt(mariadbIndex);

      case sql::Types::BIGINT:
        return static_cast<int64_t>(resultSet_->getInt64(mariadbIndex));

      case sql::Types::REAL:
      case sql::Types::FLOAT:
      case sql::Types::DOUBLE:
        return static_cast<double>(resultSet_->getDouble(mariadbIndex));

      case sql::Types::TIMESTAMP:
      case sql::Types::DATE:
      case sql::Types::TIME:
        try {
          std::string dateStr = resultSet_->getString(mariadbIndex).c_str();
          std::tm tm = {};
          std::istringstream ss(dateStr);

          if (dateStr.size() >= 19) {  // YYYY-MM-DD HH:MM:SS
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
          } else if (dateStr.size() >= 10) {  // YYYY-MM-DD
            ss >> std::get_time(&tm, "%Y-%m-%d");
          } else if (dateStr.size() >= 8) {  // HH:MM:SS
            ss >> std::get_time(&tm, "%H:%M:%S");
          } else {
            throw std::runtime_error("Unsupported date/time format");
          }

          if (ss.fail()) {
            LOG_WARN << "Failed to parse date/time string: " << dateStr;
            return dateStr;  // Return as string if parsing fails
          }

          auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
          return tp;
        } catch (std::exception& ex) {
          LOG_WARN << "Failed to parse date/time value: " << ex.what();
          return resultSet_->getString(mariadbIndex).c_str();
        }

      case sql::Types::CHAR:
      case sql::Types::VARCHAR:
      case sql::Types::LONGVARCHAR:
      case sql::Types::BINARY:
      case sql::Types::VARBINARY:
      case sql::Types::LONGVARBINARY:
      case sql::Types::DECIMAL:
      case sql::Types::NUMERIC:
      default:
        return String(resultSet_->getString(mariadbIndex).c_str());
    }
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to get value from column " << columnIndex << ": "
              << ex.what();
    throw;
  }
}

SqlValue ResultSet::getValue(const std::string& columnName) const {
  try {
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i] == columnName) {
        return getValue(static_cast<int>(i));
      }
    }

    LOG_ERROR << "Column not found: " << columnName;
    throw std::out_of_range("Column not found: " + columnName);
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to get value from column '" << columnName
              << "': " << ex.what();
    throw;
  }
}

RowData ResultSet::getRow() const {
  RowData row;

  for (int i = 0; i < columnCount_; ++i) {
    row[columns_[i]] = getValue(i);
  }

  return row;
}

std::vector<RowData> ResultSet::getAll() {
  std::vector<RowData> rows;

  try {
    bool canPosition = false;
    int currentRow = 0;

    try {
      currentRow = resultSet_->getRow();
      canPosition = true;
    } catch (SqlException&) {
      canPosition = false;
    }

    if (canPosition) {
      bool wasBeforeFirst = (currentRow == 0);

      resultSet_->beforeFirst();

      while (next()) {
        rows.push_back(getRow());
      }

      if (wasBeforeFirst) {
        resultSet_->beforeFirst();
      } else {
        resultSet_->absolute(currentRow);
      }
    } else {
      while (next()) {
        rows.push_back(getRow());
      }
    }

    return rows;
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed to fetch all rows: " << ex.what();
    throw;
  }
}

int ResultSet::getColumnCount() const {
  return columnCount_;
}

std::vector<std::string> ResultSet::getColumnNames() const {
  return columns_;
}

void ResultSet::forEach(const RowHandler& callback) {
  try {
    bool canPosition = false;
    int currentRow = 0;

    try {
      currentRow = resultSet_->getRow();
      canPosition = true;
    } catch (SqlException&) {
      canPosition = false;
    }

    if (canPosition) {
      bool wasBeforeFirst = (currentRow == 0);

      resultSet_->beforeFirst();

      while (next()) {
        callback(getRow());
      }

      if (wasBeforeFirst) {
        resultSet_->beforeFirst();
      } else {
        resultSet_->absolute(currentRow);
      }
    } else {
      while (next()) {
        callback(getRow());
      }
    }
  } catch (SqlException& ex) {
    LOG_ERROR << "Failed during forEach processing: " << ex.what();
    throw;
  }
}
