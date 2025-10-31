// based on N4189: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4189
// eventually this will be substituted by std::unique_resource onces it becomes a standard

module;

#include <type_traits>
#include <utility>

export module utils.unique_resource;



namespace gpsxre
{

export template<typename R, typename D>
class unique_resource
{
public:
    explicit unique_resource(R &&resource, D &&deleter, bool should_run = true) noexcept
        : _resource(std::move(resource))
        , _deleter(std::move(deleter))
        , _shouldRun(should_run)
    {
    }

    unique_resource(unique_resource &&other) noexcept
        : _resource(std::move(other._resource))
        , _deleter(std::move(other._deleter))
        , _shouldRun(other._shouldRun)
    {
        other.release();
    }

    unique_resource &operator=(unique_resource &&other) noexcept(noexcept(reset()))
    {
        reset();

        _deleter = std::move(other._deleter);
        _resource = std::move(other._resource);
        _shouldRun = other._shouldRun;

        other.release();

        return *this;
    }

    ~unique_resource() noexcept(noexcept(reset()))
    {
        reset();
    }

    void reset() noexcept(noexcept(get_deleter()(_resource)))
    {
        if(_shouldRun)
        {
            _shouldRun = false;
            get_deleter()(_resource);
        }
    }

    void reset(R &&resource) noexcept(noexcept(reset()))
    {
        reset();

        _resource = std::move(resource);
        _shouldRun = true;
    }

    R const &release() noexcept
    {
        _shouldRun = false;
        return get();
    }

    R const &get() const noexcept
    {
        return _resource;
    }

    operator R const &() const noexcept
    {
        return _resource;
    }

    R operator->() const noexcept
    {
        return _resource;
    }

    std::add_lvalue_reference_t<std::remove_pointer_t<R>> operator*() const
    {
        return *_resource;
    }

    const D &get_deleter() const noexcept
    {
        return _deleter;
    }

private:
    R _resource;
    D _deleter;
    bool _shouldRun;

    unique_resource &operator=(unique_resource const &) = delete;
    unique_resource(unique_resource const &) = delete;
};

export template<typename R, typename D>
auto make_unique_resource(R &&r, D &&d) noexcept
{
    return unique_resource<R, std::remove_reference_t<D>>(std::move(r), std::forward<std::remove_reference_t<D>>(d), true);
}

export template<typename R, typename D>
auto make_unique_resource_checked(R r, R invalid, D d) noexcept
{
    bool should_run = !(r == invalid);
    return unique_resource<R, D>(std::move(r), std::move(d), should_run);
}

}
