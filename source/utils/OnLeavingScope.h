/**
* The contents of this file are based on the article posted at the
* following location:
*
*   http://crascit.com/2015/06/03/on-leaving-scope-part-2/
*
* The material in that article has some commonality with the code made
* available as part of Facebook's folly library at:
*
*   https://github.com/facebook/folly/blob/master/folly/ScopeGuard.h
*
* Furthermore, similar material is currently part of a draft proposal
* to the C++ standards committee, referencing the same work by Andrei
* Alexandresu that led to the folly implementation. The draft proposal
* can be found at:
*
*   http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4189.pdf
*
* With the above in mind, the content below is made available under
* the same license terms as folly to minimize any legal concerns.
* Should there be ambiguity over copyright ownership between Facebook
* and myself for any material included in this file, it should be
* interpreted that Facebook is the copyright owner for the ambiguous
* section of code concerned.
*
*   Craig Scott
*   3rd June 2015
*
* ----------------------------------------------------------------------
*
* Copyright 2015 Craig Scott
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef CRASCIT_ONLEAVINGSCOPE_H
#define CRASCIT_ONLEAVINGSCOPE_H

#include <type_traits>
#include <utility>


/**
* This class is intended to be used only to create a local object on the
* stack. It accepts a function object in its constructor and will invoke
* that function object in its destructor. The class provides a move
* constructor so it can be used with the onLeavingScope factory function,
* but it cannot be copied.
*
* Do no use this function directly, use the onLeavingScope() factory
* function instead.
*/
template<typename Func>
class OnLeavingScope {
public:
    // Prevent copying
    OnLeavingScope(const OnLeavingScope &) = delete;
    OnLeavingScope &operator=(const OnLeavingScope &) = delete;

    // Allow moving
    OnLeavingScope(OnLeavingScope &&other) : m_func(std::move(other.m_func)),
                                             m_owner(other.m_owner) {
        other.m_owner = false;
    }

    OnLeavingScope(const Func &f) : m_func(f),
                                    m_owner(true) {
    }
    OnLeavingScope(Func &&f) : m_func(std::move(f)),
                               m_owner(true) {
    }
    ~OnLeavingScope() {
        if (m_owner)
            m_func();
    }

private:
    Func m_func;
    bool m_owner;
};


/**
* Factory function for creating an OnLeavingScope object. It is intended
* to be used like so:
*
*     auto cleanup = onLeavingScope(...);
*
* where the ... could be a lambda function, function object or pointer to
* a free function to be invoked when the cleanup object goes out of scope.
* The function object must take no function arguments, but can return any
* type (the return value is ignored).
*
* The \a Func template parameter would rarely, if ever, be manually
* specified. Normally, it would be deduced automatically by the compiler
* from the object passed as the function argument.
*/
template<typename Func>
OnLeavingScope<typename std::decay<Func>::type> onLeavingScope(Func &&f) {
    return OnLeavingScope<typename std::decay<Func>::type>(std::forward<Func>(f));
}

#endif // CRASCIT_ONLEAVINGSCOPE_H
