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

#include <random>
#include "cppmicroservices/Framework.h"
#include "cppmicroservices/FrameworkEvent.h"
#include "cppmicroservices/FrameworkFactory.h"
#include "cppmicroservices/BundleContext.h"
#include "cppmicroservices/servicecomponent/ComponentConstants.hpp"
#include "cppmicroservices/servicecomponent/runtime/ServiceComponentRuntime.hpp"
#include "../src/manager/ReferenceManagerImpl.hpp"

#include "Mocks.hpp"
#include "ConcurrencyTestUtil.hpp"
#include "TestUtils.hpp"
#include "TestInterfaces/Interfaces.hpp"

template <typename Task, typename Predicate>
bool RepeatTaskUntilOrTimeout(Task&& t, Predicate&& p)
{
  using namespace std::chrono;
  auto startTime = system_clock::now();
  do
  {
    t();
    duration<double> duration = system_clock::now() - startTime;
    if(duration > milliseconds(30000))
    {
      return false;
    }
  } while(!p());
  return true;
}


namespace scr = cppmicroservices::service::component::runtime;

using cppmicroservices::service::component::ComponentConstants::REFERENCE_SCOPE_PROTOTYPE_REQUIRED;

namespace cppmicroservices {
namespace scrimpl {
// The fixture for testing class ReferenceManagerImpl.

const std::string FakeComponentConfigName = "foobar";

namespace metadata {

std::ostream& operator<<(std::ostream& os, const ReferenceMetadata& data)
{
  return os << "name           = " << data.name << std::endl
            << "target         = " << data.target << std::endl
            << "interfaceName  = " << data.interfaceName << std::endl
            << "cardinality    = " << data.cardinality << std::endl
            << "policy         = " << data.policy << std::endl
            << "policyOption   = " << data.policyOption << std::endl
            << "scope          = " << data.scope << std::endl
            << "minCardinality = " << data.minCardinality << std::endl
            << "maxCardinality = " << data.maxCardinality << std::endl;
}

}

class ReferenceManagerImplTest : public ::testing::TestWithParam<metadata::ReferenceMetadata> {
protected:
  ReferenceManagerImplTest() : framework(cppmicroservices::FrameworkFactory().NewFramework())
  { }
  virtual ~ReferenceManagerImplTest() = default;

  virtual void SetUp() {
    framework.Start();
  }

  virtual void TearDown() {
    framework.Stop();
    framework.WaitForStop(std::chrono::milliseconds::zero());
  }

  cppmicroservices::Framework& GetFramework() { return framework; }
private:
  cppmicroservices::Framework framework;
};

// utility method for creating different types of reference metadata objects used in testing
metadata::ReferenceMetadata CreateFakeReferenceMetadata(const std::string& policy
                                                        , const std::string& policyOption
                                                        , const std::string& cardinality
                                                        , std::string refName = "ref"
                                                       )
{
  metadata::ReferenceMetadata fakeMetadata{};
  fakeMetadata.name = std::move(refName);
  fakeMetadata.interfaceName = us_service_interface_iid<dummy::Reference1>();
  fakeMetadata.policy = policy;
  fakeMetadata.policyOption = policyOption;
  fakeMetadata.cardinality = cardinality;

  if (cardinality == "0..1")
  {
    fakeMetadata.minCardinality = 0;
    fakeMetadata.maxCardinality = 1;
  }
  else if (cardinality == "0..n")
  {
    fakeMetadata.minCardinality = 0;
    fakeMetadata.maxCardinality = std::numeric_limits<unsigned int>::max();
  }
  else if (cardinality == "1..1")
  {
    fakeMetadata.minCardinality = 1;
    fakeMetadata.maxCardinality = 1;
  }
  else
  {
    fakeMetadata.minCardinality = 1;
    fakeMetadata.maxCardinality = std::numeric_limits<unsigned int>::max();
  }
  return fakeMetadata;
}

INSTANTIATE_TEST_SUITE_P(ReferenceManagerParameterized, ReferenceManagerImplTest
                         , testing::Values(CreateFakeReferenceMetadata("static"   , "reluctant", "0..1")
                                           , CreateFakeReferenceMetadata("static" , "reluctant", "1..1")
                                           , CreateFakeReferenceMetadata("static" , "greedy"   , "0..1")
                                           , CreateFakeReferenceMetadata("static" , "greedy"   , "1..1")
                                           , CreateFakeReferenceMetadata("dynamic", "reluctant", "0..1")
                                           , CreateFakeReferenceMetadata("dynamic", "reluctant", "1..1")
                                           , CreateFakeReferenceMetadata("dynamic", "greedy"   , "0..1")
                                           , CreateFakeReferenceMetadata("dynamic", "greedy"   , "1..1")
                                          ));

TEST_P(ReferenceManagerImplTest, TestConstructor)
{
  metadata::ReferenceMetadata mockReferenceMetadata{};
  auto fakeLogger = std::make_shared<FakeLogger>();
  EXPECT_THROW({
      ReferenceManagerImpl refManager(mockReferenceMetadata
                                      , BundleContext()
                                      , fakeLogger
                                      , FakeComponentConfigName);
    }, std::invalid_argument) << "Invalid bundle context must result in a throw";
  EXPECT_THROW({
      ReferenceManagerImpl refManager(mockReferenceMetadata
                                      , GetFramework().GetBundleContext()
                                      , nullptr
                                      , FakeComponentConfigName);
    }, std::invalid_argument) << "Invalid logger object must result in a throw";
  EXPECT_NO_THROW({
      mockReferenceMetadata.name = "Foo";
      mockReferenceMetadata.target = "(objectclass=Foo)";
      ReferenceManagerImpl refManager(mockReferenceMetadata
                                      , GetFramework().GetBundleContext()
                                      , fakeLogger
                                      , FakeComponentConfigName);
      EXPECT_EQ(refManager.GetReferenceName(), "Foo");
      EXPECT_EQ(refManager.GetLDAPString(), "(objectclass=Foo)");
    }) << "No throw expected when valid objects are passed to ReferenceManager constructor";
}

TEST_P(ReferenceManagerImplTest, TestIsSatisfied)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  // 0..x cardinality (optional unary/multiple dependency)
  // create a reference manager and check IsSatisfied is true.
  // 1..x cardinality (default - mandatory unary dependency)
  // create a reference manager and check IsSatisfied is false.
  // Register a service and check IsSatisfied is true
  {
    auto fakeMetadata = GetParam();
    ReferenceManagerImpl refManager(fakeMetadata
                                    , GetFramework().GetBundleContext()
                                    , fakeLogger
                                    , FakeComponentConfigName);
    EXPECT_EQ(refManager.IsSatisfied(), (refManager.IsOptional() ? true : false)) << "Initial state is SATISFIED only if cardinality is optional";
    auto reg = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
    EXPECT_EQ(refManager.IsSatisfied(), true) << "State expected to be SATISFIED after service registration";
    reg.Unregister();
  }
}

TEST_P(ReferenceManagerImplTest, TestListenerCallbacks)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  // Reluctant policy option
  // create a reference manager and check IsSatisfied is false.
  // register a service and verify the RefSatisfiedCallback is called
  // register another service with higher ranking. Observe no change.
  {
    auto fakeMetadata = GetParam();
    ReferenceManagerImpl refManager(fakeMetadata
                                    , GetFramework().GetBundleContext()
                                    , fakeLogger
                                    , FakeComponentConfigName);
    EXPECT_EQ(refManager.IsSatisfied(), refManager.IsOptional()) << "Initial state is SATISFIED only for optional cardinality";
    int satisfiedNotificationCount(0);
    int unsatisfiedNotificationCount(0);
    auto resetCounters = [&satisfiedNotificationCount, &unsatisfiedNotificationCount]() {
                           satisfiedNotificationCount = 0;
                           unsatisfiedNotificationCount = 0;
                         };

    ListenerTokenId token = refManager.RegisterListener([&](const RefChangeNotification& notification) {
                                                          switch (notification.event) {
                                                            case RefEvent::BECAME_SATISFIED:
                                                              satisfiedNotificationCount++;
                                                              break;
                                                            case RefEvent::BECAME_UNSATISFIED:
                                                              unsatisfiedNotificationCount++;
                                                              break;
                                                            default:
                                                              break;
                                                          }
                                                        });
    // Expect a callback as soon as registered for optional dependency
    EXPECT_EQ(satisfiedNotificationCount, (refManager.IsOptional() ? 1: 0)) << "SATISFIED notification expected for optional cardinality";
    EXPECT_EQ(unsatisfiedNotificationCount, 0) << "No UNSATISFIED notification expected";
    resetCounters();

    // Register first service
    // optional, static-reluctant - no change in state & no callback expected
    // optional, static-greedy - no change in final state, one call back each for unsatisfied and satisfied
    // mandatory, static-reluctant - state change & one satisfied callback
    // mandatory, static-greedy - state change & one satisfied callback
    auto reg = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
    EXPECT_EQ(refManager.IsSatisfied(), true);
    EXPECT_EQ(unsatisfiedNotificationCount, refManager.IsOptional()  && (fakeMetadata.policyOption == "greedy") ? 1 : 0) << "UNSATISFIED notification expected only for optional-greedy";
    EXPECT_EQ(satisfiedNotificationCount, (refManager.IsOptional() && fakeMetadata.policyOption == "reluctant") ? 0 : 1) << "SATISFIED notification expected except for optional-reluctant";
    resetCounters();

    // Register second service with same rank
    // optional, static-reluctant - no state change & no callback expected
    // optional, static-greedy - no state change, no callback expected
    // mandatory, static-reluctant - no state change, no callback expected
    // mandatory, static-greedy - no state change, no callback expected
    auto reg1 = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
    EXPECT_EQ(refManager.IsSatisfied(), true);
    EXPECT_EQ(satisfiedNotificationCount, 0) << "no notification expected since the service registered has teh same rank";
    EXPECT_EQ(unsatisfiedNotificationCount, 0) << "no notification expected since the service registered has teh same rank";
    resetCounters();

    // Register third service with higher rank
    // optional, static-reluctant - no state change & no callback expected
    // optional, static-greedy - no state change, two callbacks expected in sequence UNSATISFIED, SATISFIED
    // mandatory, static-reluctant - no state change, no callback expected
    // mandatory, static-greedy - no state change, two callbacks expected in sequence UNSATISFIED, SATISFIED
    auto reg2 = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>(), {{Constants::SERVICE_RANKING, Any(10)}});
    EXPECT_EQ(refManager.IsSatisfied(), true);
    EXPECT_EQ(unsatisfiedNotificationCount, fakeMetadata.policyOption == "greedy" ? 1 :0) << "UNSATISFIED notification must be sent only for greedy policy";
    EXPECT_EQ(satisfiedNotificationCount, fakeMetadata.policyOption == "greedy" ? 1: 0) << "SATISFIED notification must be sent only for greedy policy";;
    resetCounters();

    // unregister service 1.
    // optional, static-reluctant - not bound so no change
    // optional, static-greedy - not bound so no change
    // mandatory, static-reluctant - bound, expect callback with UNSATISFIED
    // mandatory, static-greedy - not bound so no change
    reg.Unregister();
    EXPECT_EQ(unsatisfiedNotificationCount, fakeMetadata.cardinality == "1..1" && fakeMetadata.policyOption == "reluctant" ? 1 : 0) << "UNSATISFIED notification must be sent only for mandatory-unary-static-reluctant";
    EXPECT_EQ(satisfiedNotificationCount, fakeMetadata.cardinality == "1..1" && fakeMetadata.policyOption == "reluctant" ? 1 : 0) << "SATISFIED notification must be sent only for mandatory-unary-static-reluctant";
    resetCounters();

    // unregister service 2.
    // optional, static-reluctant - not bound so no change
    // optional, static-greedy - not bound so no change
    // mandatory, static-reluctant - not bound so no change
    // mandatory, static-greedy - not bound so no change
    reg1.Unregister();
    EXPECT_EQ(satisfiedNotificationCount, 0) << "No changes in bindings so no SATISFIED notification expected";
    EXPECT_EQ(unsatisfiedNotificationCount, 0) << "No changes in bindings so no UNSATISFIED notification expected";
    resetCounters();

    // unregister service 3.
    // optional, static-reluctant - not bound so no change
    // optional, static-greedy - bound, expect callback with UNSATISFIED & SATISFIED
    // mandatory, static-reluctant - not bound initially but bound after reg is unregistered.
    // mandatory, static-greedy - bound, expect callback with UNSATISFIED
    reg2.Unregister();
    EXPECT_EQ(unsatisfiedNotificationCount, refManager.IsOptional() && fakeMetadata.policyOption == "reluctant" ? 0 : 1) << "UNSATISFIED notification must be sent except for optional-static-reluctant";
    EXPECT_EQ(satisfiedNotificationCount, refManager.IsOptional() && fakeMetadata.policyOption == "greedy" ? 1 : 0) << "SATISFIED notification must be sent only for optional-static-greedy";;
    resetCounters();

    refManager.UnregisterListener(token);
  }
}

/*
 * Concurrency Tests
 */
TEST_P(ReferenceManagerImplTest, TestConcurrentSatisfied)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  auto fakeMetadata = GetParam();
  ReferenceManagerImpl refManager(fakeMetadata
                                  , GetFramework().GetBundleContext()
                                  , fakeLogger
                                  , FakeComponentConfigName);

  std::function<ServiceRegistration<dummy::Reference1>()> func = [bc]() mutable {
                                                                   return bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
                                                                 };
  auto registrations = ConcurrentInvoke(func);

  EXPECT_TRUE(refManager.IsSatisfied()) << "Reference Manager must be in satisfied state after concurrent service registrations";
  EXPECT_EQ(refManager.GetBoundReferences().size(), (refManager.IsOptional() && fakeMetadata.policyOption == "reluctant") ? 0ul : 1ul) << "A reference must be bound unless the cardinality is optional and binding policy is static";
  for(auto& reg : registrations)
  {
    reg.Unregister();
  }
}

TEST_P(ReferenceManagerImplTest, TestConcurrentUnsatisfied)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  auto fakeMetadata = GetParam();
  ReferenceManagerImpl refManager(fakeMetadata
                                  , GetFramework().GetBundleContext()
                                  , fakeLogger
                                  , FakeComponentConfigName);
  std::promise<void> go;
  std::shared_future<void> ready(go.get_future());
  int numCalls = std::max(64u, std::thread::hardware_concurrency());
  std::vector<std::promise<void>> readies(numCalls);
  std::vector<std::future<void>> serviceUnregFutures(numCalls);
  std::vector<ServiceRegistration<dummy::Reference1>> sRegs(numCalls);
  try
  {
    for(int i=0; i<numCalls; i++)
    {
      sRegs[i] = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
    }
    EXPECT_TRUE(refManager.IsSatisfied()) << "Reference manager must be satisfied after service registrations";
    EXPECT_FALSE(refManager.GetTargetReferences().empty()) << "since multiple services are registered, target references must be non-empty";
    if(!refManager.IsOptional())
    {
      EXPECT_FALSE(refManager.GetBoundReferences().empty()) << "atleast one reference must be bound";
    }
    for(int i=0; i<numCalls; i++)
    {
      serviceUnregFutures[i] = std::async(std::launch::async,
                                          [ready, &readies, i, &sRegs]()
                                          {
                                            readies[i].set_value();
                                            ready.wait();
                                            sRegs[i].Unregister();
                                          });
    }
    for(auto&& ready : readies)
    {
      ready.get_future().wait();
    }
    go.set_value();
    for(auto&& sUnregFuture : serviceUnregFutures)
    {
      sUnregFuture.wait();
    }
  }
  catch(const std::exception& ex)
  {
    ASSERT_FALSE(true) << "Error: unexpected exception caught ... " << ex.what();
    go.set_value();
    throw std::current_exception();
  }

  EXPECT_EQ(refManager.IsSatisfied(), refManager.IsOptional()) << "Reference manager must be satisfied only if cardinality is soptional";
  EXPECT_EQ(refManager.GetTargetReferences().size(), 0ul) << "matched references must be 0 since all services are unregistered";
  EXPECT_EQ(refManager.GetBoundReferences().size(), 0ul) << "bound references must be 0 since all services are unregistered";
}

TEST_P(ReferenceManagerImplTest, TestConcurrentSatisfiedUnsatisfied)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  auto fakeMetadata = GetParam();
  ReferenceManagerImpl refManager(fakeMetadata
                                  , GetFramework().GetBundleContext()
                                  , fakeLogger
                                  , FakeComponentConfigName);

  std::function<ServiceRegistration<dummy::Reference1>()> func = [bc]() mutable {
                                                                   ServiceRegistration<dummy::Reference1> sReg;
                                                                   std::random_device rd;
                                                                   std::mt19937 gen(rd());
                                                                   std::uniform_int_distribution<unsigned int> dis(20,80);
                                                                   int randVal = dis(gen); // random number in range [20, 80)
                                                                   // if randval is odd, a service exists in service registry by
                                                                   // the end of this loop. If randVal is even, no service is
                                                                   // registered by this thread, by the end of this loop
                                                                   for(int j = 0; j < randVal; j++)
                                                                   {
                                                                     if((j & 0x1) == 0x0)
                                                                     {
                                                                       sReg = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>());
                                                                     }
                                                                     else
                                                                     {
                                                                       sReg.Unregister();
                                                                     }
                                                                   }
                                                                   return sReg;
                                                                 };
  auto registrations = ConcurrentInvoke(func);

  auto registeredServiceRefs = bc.GetServiceReferences<dummy::Reference1>();
  auto registeredServiceCount = registeredServiceRefs.size();
  // statuc-reluctant, mandatory-unary - depends on which thread's service was bound
  // static-reluctant, optional-unary - none of the services are bound
  if(refManager.IsOptional() &&
     fakeMetadata.policyOption == "reluctant")
  {
    EXPECT_EQ(refManager.GetBoundReferences().size(), 0ul) << "No references must be bound for OPTIONAL cardinality with RELUCTANT policy";
  }
  // static-greedy, optional-unary - the service with the highest rank is bound
  // static-greedy, mandatory-unary - the service with the highest rank is bound
  if(refManager.IsOptional() &&
     fakeMetadata.policyOption == "greedy")
  {
    EXPECT_EQ(refManager.GetBoundReferences().size(), (registeredServiceCount ? 1ul : 0ul)) << "If any services are available, bound services must not be zero";
  }


  if(refManager.IsOptional())
  {
    EXPECT_TRUE(refManager.IsSatisfied()) << "Ref should only be satisfied if it is optional";;
  }
  EXPECT_EQ(refManager.GetTargetReferences().size(), registeredServiceCount) << "TargetReferences must be the same as any available services in the framework";
}
TEST_P(ReferenceManagerImplTest, TestTrackerWithScope_PrototypeRequired)
{
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();
  auto fakeMetadata = GetParam();
  fakeMetadata.scope = REFERENCE_SCOPE_PROTOTYPE_REQUIRED;
  ReferenceManagerImpl refManager(fakeMetadata
                                  , GetFramework().GetBundleContext()
                                  , fakeLogger
                                  , FakeComponentConfigName);

  // when the reference scope is 'prototype_required', the reference manager's
  // tracker must only bind to the service published with scope==prototype
  ASSERT_TRUE(refManager.GetTargetReferences().empty());
  auto reg = bc.RegisterService<dummy::Reference1>(ToFactory(std::make_shared<MockFactory>()),
                                                   {{cppmicroservices::Constants::SERVICE_SCOPE, cppmicroservices::Constants::SCOPE_BUNDLE}});
  ASSERT_TRUE(refManager.GetTargetReferences().empty()) << "service registered with BUNDLE scope must not match the tracker";
  reg.Unregister();

  reg = bc.RegisterService<dummy::Reference1>(std::make_shared<dummy::Reference1>()); // singleton scope
  ASSERT_TRUE(refManager.GetTargetReferences().empty()) << "service registered with SINGLETON scope must not match the tracker";
  reg.Unregister();

  reg = bc.RegisterService<dummy::Reference1>(ToFactory(std::make_shared<MockFactory>()),
                                              {{cppmicroservices::Constants::SERVICE_SCOPE, cppmicroservices::Constants::SCOPE_PROTOTYPE}});
  ASSERT_FALSE(refManager.GetTargetReferences().empty()) << "service registered with PROTOTYPE scope must match the tracker";
  reg.Unregister();
}

TEST_F(ReferenceManagerImplTest, TestTargetProperty)
{
  namespace constants = cppmicroservices::Constants;
  auto fakeMetadata   = CreateFakeReferenceMetadata("static", "reluctant", "1..1");
  auto bc             = GetFramework().GetBundleContext();
  auto fakeLogger     = std::make_shared<FakeLogger>();

  fakeMetadata.target = "(foo=bar)";
  ReferenceManagerImpl refManager {
    fakeMetadata
    , bc
    , fakeLogger
    , FakeComponentConfigName
  };
        
  ASSERT_FALSE(refManager.IsSatisfied());
        
  (void)bc.RegisterService<dummy::Reference1>(ToFactory(std::make_shared<MockFactory>())
                                              , {{constants::SERVICE_SCOPE, constants::SCOPE_BUNDLE}});
  ASSERT_FALSE(refManager.IsSatisfied());

  (void)bc.RegisterService<dummy::Reference1>(ToFactory(std::make_shared<MockFactory>())
                                              , {{"foo", std::string("bar")}});
  ASSERT_TRUE(refManager.IsSatisfied());
}

// A service dependency cannot be satisfied by a service published from the same component
// configuration.
TEST_F(ReferenceManagerImplTest, TestSelfSatisfy)
{
  auto fakeMetadata = CreateFakeReferenceMetadata("static", "reluctant", "1..1");
  fakeMetadata.interfaceName = "dummy::Reference1";
  fakeMetadata.name = "dummy_ref";
  auto bc = GetFramework().GetBundleContext();
  auto fakeLogger = std::make_shared<FakeLogger>();

  ReferenceManagerImpl refManager {
    fakeMetadata
    , bc
    , fakeLogger
    , FakeComponentConfigName
  };

  auto reg = bc.RegisterService<dummy::Reference1>(
    std::make_shared<dummy::Reference1>());
  EXPECT_FALSE(refManager.IsSatisfied())
    << "State expected to be UNSATISFIED after service registration";
  reg.Unregister();
}

struct CmpSymbolName
{
  CmpSymbolName(std::string c)
    : cmpTo(c)
  {
  }

  bool operator()(const cppmicroservices::Bundle& b)
  {
    auto symbolicName = b.GetSymbolicName();
    return (cmpTo == symbolicName);
  }
private:
  std::string cmpTo;
};

void InstallAndStartDS(::cppmicroservices::BundleContext frameworkCtx)
{
  std::vector<cppmicroservices::Bundle> bundles;
  auto dsPluginPath = test::GetDSRuntimePluginFilePath();

#if defined(US_BUILD_SHARED_LIBS)
  bundles = frameworkCtx.InstallBundles(dsPluginPath);
#else
  bundles = frameworkCtx.GetBundles();
#endif

  for (auto b : bundles) {
      b.Start();
  }
}


TEST_F(ReferenceManagerImplTest, TestDynamicGreedy)
{
  auto bc = GetFramework().GetBundleContext();
  InstallAndStartDS(bc);

  auto testBundle = test::InstallAndStartBundle(bc, "TestBundleDSTOI20");
  EXPECT_FALSE(bc.GetServiceReference<test::Interface2>()) << "Service must not be available before it's dependency";
  auto dsRef = bc.GetServiceReference<scr::ServiceComponentRuntime>();
  EXPECT_TRUE(dsRef);
  auto dsRuntimeService = bc.GetService<scr::ServiceComponentRuntime>(dsRef);
  auto compDescDTO = dsRuntimeService->GetComponentDescriptionDTO(testBundle, "sample::ServiceComponent20");
  auto compConfigDTOs = dsRuntimeService->GetComponentConfigurationDTOs(compDescDTO);
  EXPECT_EQ(compConfigDTOs.size(), 1ul);
  EXPECT_EQ(compConfigDTOs.at(0).state, scr::dto::ComponentState::UNSATISFIED_REFERENCE);

  auto depBundle = test::InstallAndStartBundle(bc, "TestBundleDSTOI21");
  auto result = RepeatTaskUntilOrTimeout([&compConfigDTOs, &dsRuntimeService, &compDescDTO]()
                                         {
                                           compConfigDTOs = dsRuntimeService->GetComponentConfigurationDTOs(compDescDTO);
                                         }, [&compConfigDTOs]()->bool
                                            {
                                              return compConfigDTOs.at(0).state == scr::dto::ComponentState::ACTIVE;
                                            });

  ASSERT_TRUE(result) << "Timed out waiting for state to change to ACTIVE after the dependency became available";
  auto svcRef = bc.GetServiceReference<test::Interface2>();
  ASSERT_TRUE(svcRef);
  auto svc = bc.GetService<test::Interface2>(svcRef);
  ASSERT_TRUE(svc);
  EXPECT_NO_THROW(svc->ExtendedDescription());
  
  depBundle.Stop();
  EXPECT_FALSE(bc.GetServiceReference<test::Interface2>()) << "Service should now NOT be available";
  EXPECT_THROW(svc->ExtendedDescription(), std::runtime_error);
}

}}
