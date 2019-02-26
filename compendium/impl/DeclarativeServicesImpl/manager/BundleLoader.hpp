/*=============================================================================

 Library: CppMicroServices

 Copyright (c) The CppMicroServices developers. See the COPYRIGHT
 file at the top-level directory of this distribution and at
 https://github.com/CppMicroServices/CppMicroServices/COPYRIGHT .

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 =============================================================================*/

#ifndef __BUNDLELOADER_HPP__
#define __BUNDLELOADER_HPP__

#include <map>
#include "ServiceComponent/detail/ComponentInstance.hpp"
#include "ConcurrencyUtil.hpp"

using cppmicroservices::service::component::detail::ComponentInstance;
//typedef ComponentInstance*(*NewComponentInstanceFuncPtr)();
//typedef void(*DeleteComponentInstanceFuncPtr)(ComponentInstance*);

namespace cppmicroservices {
  namespace scrimpl {
    /**
     * Method to load and find the extern C helper functions used to create and
     * delete {@link ComponentInstance} objects associated with a component from
     * a given {@link Bundle}
     *
     * \param compName is the fully qualified C++ class name of the component
     * \param fromBundle is the bundle where the component is located
     *
     * \throws \c std::runtime_error on failure to load the bundle binary.
     *         \c std::runtime_error if the entry points for \c compName are
     *         not found in the bundle \c fromBundle
     *         \c std::invalid_argument if location of \c fromBundle cannot be
     *         converted to UTF16 on the Windows platform
     */
    std::tuple<std::function<ComponentInstance*(void)>, std::function<void(ComponentInstance*)>> GetComponentCreatorDeletors(const std::string& compClassName,
                                                                                                                             const cppmicroservices::Bundle& fromBundle);
  }
}
#endif /* __BUNDLELOADER_HPP__ */