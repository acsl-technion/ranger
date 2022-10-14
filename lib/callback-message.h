#ifndef CALLBACK_MESSAGE_H
#define CALLBACK_MESSAGE_H

#include <functional>
#include <vector>

template<typename P, typename T>
class callback_message {
public:

    using function_type = std::function<void(P &, const T&)>;
    using function_type_args = std::function<void(P &, const T&, void *)>;
    T msg;

private:

    using arg_pair = std::pair<function_type_args, void*>;

    std::vector<function_type> cb_vec;
    std::vector<arg_pair> arg_vec;
public:

    void
    add_listener(function_type cb)
    {
        cb_vec.push_back(cb);
    }

    void
    add_listener(function_type_args cb, void *args)
    {
        arg_vec.push_back(arg_pair(cb, args));
    }

    void
    publish(P &parent, const T &msg)
    {
        this->msg = msg;
        for (auto &it : cb_vec) {
            it(parent, this->msg);
        }
        for (arg_pair &it : arg_vec) {
            it.first(parent, this->msg, it.second);
        }
    }

    void
    publish(P &parent)
    {
        for (auto &it : cb_vec) {
            it(parent, this->msg);
        }
        for (arg_pair &it : arg_vec) {
            it.first(parent, this->msg, it.second);
        }
    }
};

#endif
