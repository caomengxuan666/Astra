#include <iostream>
#include <memory>
#include <typeindex>

struct Any {
    Any(void) : m_tpIndex(std::type_index(typeid(void))) {}
    Any(const Any &that) : m_ptr(that.Clone()), m_tpIndex(that.m_tpIndex) {}
    Any(Any &&that) : m_ptr(std::move(that.m_ptr)), m_tpIndex(that.m_tpIndex) {}

    // 创建智能指针时，对于一般的类型，通过 std::decay 来移除引用和 cv 符（即 const/volatile），从而获取原始类型
    template<typename U, class = typename std::enable_if<!std::is_same<typename std::decay<U>::type, Any>::value, U>::type>
    Any(U &&value) : m_ptr(new Derived<typename std::decay<U>::type>(std::forward<U>(value))),
                     m_tpIndex(std::type_index(typeid(typename std::decay<U>::type))) {}

    bool IsNull() const { return !bool(m_ptr); }

    /// @note 类型不相同
    template<class U>
    bool Is() const {
        return m_tpIndex == std::type_index(typeid(U));
    }

    // 将 Any 转换为实际的类型
    template<class U>
    U &AnyCast() {
        if (!Is<U>()) {
            std::cout << "can not cast to " << typeid(U).name() << " from " << m_tpIndex.name() << std::endl;
            throw std::bad_cast();
        }

        /// @note 将基类指针转为实际的派生类型
        auto derived = dynamic_cast<Derived<U> *>(m_ptr.get());
        return derived->m_value;///< 获取原始数据
    }

    Any &operator=(const Any &a) {
        if (m_ptr == a.m_ptr)
            return *this;

        m_ptr = a.Clone();
        m_tpIndex = a.m_tpIndex;
        return *this;
    }

private:
    struct Base;
    typedef std::unique_ptr<Base> BasePtr;///< 基类指针类型

    /// @note 基类不含模板参数
    struct Base {
        virtual ~Base() {}
        virtual BasePtr Clone() const = 0;
    };

    /// @note 派生类含有模板参数
    template<typename T>
    struct Derived : Base {
        template<typename U>
        Derived(U &&value) : m_value(std::forward<U>(value)) {}

        /// @note 将派生类对象赋值给了基类指针，通过基类擦除了派生类的原始数据类型
        BasePtr Clone() const {
            return BasePtr(new Derived<T>(m_value));///< 用 unique_ptr 指针进行管理
        }

        T m_value;
    };

    BasePtr Clone() const {
        if (m_ptr != nullptr)
            return m_ptr->Clone();

        return nullptr;
    }

    BasePtr m_ptr;
    std::type_index m_tpIndex;
};
