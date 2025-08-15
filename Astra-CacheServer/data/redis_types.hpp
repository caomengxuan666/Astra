#pragma once

#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <optional>


namespace Astra {
    namespace data {

        // Hash类型实现
        class AstraHash {
        public:
            AstraHash() = default;

            bool HSet(const std::string &field, const std::string &value) {
                bool isNew = (data_.find(field) == data_.end());
                data_[field] = value;
                return isNew;
            }

            std::optional<std::string> HGet(const std::string &field) const {
                auto it = data_.find(field);
                if (it != data_.end()) {
                    return it->second;
                }
                return std::nullopt;
            }

            bool HDelete(const std::string &field) {
                return data_.erase(field) > 0;
            }

            bool HExists(const std::string &field) const {
                return data_.find(field) != data_.end();
            }

            size_t Size() const {
                return data_.size();
            }

            size_t HLen()const{
                return Size();
            }

            std::map<std::string, std::string> HGetAll() const {
                return data_;
            }

            std::vector<std::string> GetKeys() const {
                std::vector<std::string> keys;
                for (const auto &pair : data_) {
                    keys.push_back(pair.first);
                }
                return keys;
            }

            std::vector<std::string> GetValues() const {
                std::vector<std::string> values;
                for (const auto &pair : data_) {
                    values.push_back(pair.second);
                }
                return values;
            }

            std::string Serialize() const {
                std::ostringstream oss;
                oss << "hash:";
                for (const auto &[field, value] : data_) {
                    oss << field.length() << ":" << field << value.length() << ":" << value;
                }
                return oss.str();
            }

            static AstraHash Deserialize(const std::string &data) {
                AstraHash hash;
                if (data.substr(0, 5) != "hash:") {
                    return hash;
                }

                size_t pos = 5; // Skip "hash:" prefix
                while (pos < data.length()) {
                    // Parse field length
                    size_t field_len_end = data.find(':', pos);
                    if (field_len_end == std::string::npos)
                        break;

                    size_t field_len = std::stoull(data.substr(pos, field_len_end - pos));
                    pos = field_len_end + 1;

                    if (pos + field_len > data.length())
                        break;
                    std::string field = data.substr(pos, field_len);
                    pos += field_len;

                    // Parse value length
                    size_t value_len_end = data.find(':', pos);
                    if (value_len_end == std::string::npos)
                        break;

                    size_t value_len = std::stoull(data.substr(pos, value_len_end - pos));
                    pos = value_len_end + 1;

                    if (pos + value_len > data.length())
                        break;
                    std::string value = data.substr(pos, value_len);
                    pos += value_len;

                    hash.data_[field] = value;
                }

                return hash;
            }

        private:
            std::map<std::string, std::string> data_;
        };

        // List类型实现
        class AstraList {
        public:
            AstraList() = default;

            // LPUSH命令：在列表头部插入元素
            size_t LPush(const std::vector<std::string> &values) {
                for (auto it = values.rbegin(); it != values.rend(); ++it) {
                    list_.push_front(*it);
                }
                return list_.size();
            }

            // RPUSH命令：在列表尾部插入元素
            size_t RPush(const std::vector<std::string> &values) {
                for (const auto &value: values) {
                    list_.push_back(value);
                }
                return list_.size();
            }

            // LPOP命令：移除并返回列表的第一个元素
            std::string LPop() {
                if (list_.empty()) {
                    return "";
                }
                std::string value = list_.front();
                list_.pop_front();
                return value;
            }

            // RPOP命令：移除并返回列表的最后一个元素
            std::string RPop() {
                if (list_.empty()) {
                    return "";
                }
                std::string value = list_.back();
                list_.pop_back();
                return value;
            }

            // LLEN命令：获取列表长度
            size_t LLen() const {
                return list_.size();
            }

            // LRANGE命令：获取列表指定范围的元素
            std::vector<std::string> LRange(long start, long stop) const {
                std::vector<std::string> result;
                if (list_.empty()) return result;

                long size = static_cast<long>(list_.size());
                // 处理负数索引
                if (start < 0) start = size + start;
                if (stop < 0) stop = size + stop;

                // 边界检查
                if (start < 0) start = 0;
                if (stop >= size) stop = size - 1;
                if (start > stop) return result;

                auto it = list_.begin();
                std::advance(it, start);
                for (long i = start; i <= stop && it != list_.end(); ++i, ++it) {
                    result.push_back(*it);
                }
                return result;
            }

            // LINDEX命令：获取列表指定位置的元素
            std::string LIndex(long index) const {
                if (list_.empty()) return "";

                long size = static_cast<long>(list_.size());
                if (index < 0) index = size + index;
                if (index < 0 || index >= size) return "";

                auto it = list_.begin();
                std::advance(it, index);
                return *it;
            }

        private:
            std::list<std::string> list_;
        };

        // Set类型实现
        class AstraSet {
        public:
            AstraSet() = default;

            // SADD命令：向集合添加元素
            int SAdd(const std::vector<std::string> &members) {
                int added = 0;
                for (const auto &member: members) {
                    if (set_.insert(member).second) {
                        added++;
                    }
                }
                return added;
            }

            // SREM命令：从集合中移除元素
            int SRem(const std::vector<std::string> &members) {
                int removed = 0;
                for (const auto &member: members) {
                    if (set_.erase(member)) {
                        removed++;
                    }
                }
                return removed;
            }

            // SCARD命令：获取集合元素数量
            size_t SCard() const {
                return set_.size();
            }

            // SMEMBERS命令：获取集合所有成员
            std::vector<std::string> SMembers() const {
                std::vector<std::string> members;
                members.reserve(set_.size());
                for (const auto &member: set_) {
                    members.push_back(member);
                }
                return members;
            }

            // SISMEMBER命令：检查元素是否在集合中
            bool SIsMember(const std::string &member) const {
                return set_.find(member) != set_.end();
            }

            // SPOP命令：随机移除并返回集合中的元素
            std::string SPop() {
                if (set_.empty()) return "";

                auto it = set_.begin();
                std::string member = *it;
                set_.erase(it);
                return member;
            }

        private:
            std::set<std::string> set_;// 使用有序set便于实现SMEMBERS的稳定输出
        };

        // ZSet类型实现（有序集合）
        class AstraZSet {
        public:
            AstraZSet() = default;

            // ZADD命令：向有序集合添加元素
            int ZAdd(const std::map<std::string, double> &members) {
                int added = 0;
                for (const auto &pair: members) {
                    const std::string &member = pair.first;
                    double score = pair.second;

                    // 查找是否已存在
                    auto member_it = member_to_score_.find(member);
                    if (member_it != member_to_score_.end()) {
                        // 如果分数不同，需要更新
                        if (member_it->second != score) {
                            // 从旧分数中移除
                            auto range = score_to_members_.equal_range(member_it->second);
                            for (auto it = range.first; it != range.second; ++it) {
                                if (it->second == member) {
                                    score_to_members_.erase(it);
                                    break;
                                }
                            }

                            // 更新分数
                            member_it->second = score;

                            // 添加到新分数
                            score_to_members_.emplace(score, member);
                        }
                    } else {
                        // 新成员
                        member_to_score_[member] = score;
                        score_to_members_.emplace(score, member);
                        added++;
                    }
                }
                return added;
            }

            // ZREM命令：从有序集合中移除元素
            int ZRem(const std::vector<std::string> &members) {
                int removed = 0;
                for (const auto &member: members) {
                    auto member_it = member_to_score_.find(member);
                    if (member_it != member_to_score_.end()) {
                        double score = member_it->second;

                        // 从分数映射中移除
                        auto range = score_to_members_.equal_range(score);
                        for (auto it = range.first; it != range.second; ++it) {
                            if (it->second == member) {
                                score_to_members_.erase(it);
                                break;
                            }
                        }

                        // 从成员映射中移除
                        member_to_score_.erase(member_it);
                        removed++;
                    }
                }
                return removed;
            }

            // ZCARD命令：获取有序集合元素数量
            size_t ZCard() const {
                return member_to_score_.size();
            }

            // ZRANGE命令：获取指定范围的成员
            std::vector<std::string> ZRange(long start, long stop) const {
                std::vector<std::string> result;
                if (score_to_members_.empty()) return result;

                long size = static_cast<long>(score_to_members_.size());
                // 处理负数索引
                if (start < 0) start = size + start;
                if (stop < 0) stop = size + stop;

                // 边界检查
                if (start < 0) start = 0;
                if (stop >= size) stop = size - 1;
                if (start > stop) return result;

                long index = 0;
                for (auto it = score_to_members_.begin(); it != score_to_members_.end() && index <= stop; ++it, ++index) {
                    if (index >= start) {
                        result.push_back(it->second);
                    }
                }
                return result;
            }

            // ZRANGEBYSCORE命令：通过分数范围获取成员
            std::vector<std::string> ZRangeByScore(double min, double max) const {
                std::vector<std::string> result;
                auto lower = score_to_members_.lower_bound(min);
                auto upper = score_to_members_.upper_bound(max);

                for (auto it = lower; it != upper; ++it) {
                    result.push_back(it->second);
                }
                return result;
            }

            // ZSCORE命令：获取成员的分数
            std::pair<bool, double> ZScore(const std::string &member) const {
                auto it = member_to_score_.find(member);
                if (it != member_to_score_.end()) {
                    return {true, it->second};
                }
                return {false, 0.0};
            }

        private:
            std::unordered_map<std::string, double> member_to_score_;// 成员到分数的映射
            std::multimap<double, std::string> score_to_members_;    // 分数到成员的映射（支持相同分数的多个成员）
        };

    }// namespace data
}// namespace Astra