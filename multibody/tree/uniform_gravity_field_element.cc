#include "drake/multibody/tree/uniform_gravity_field_element.h"

#include <vector>

#include "drake/common/autodiff.h"
#include "drake/multibody/tree/body.h"
#include "drake/multibody/tree/multibody_tree.h"

namespace drake {
namespace multibody {

template <typename T>
UniformGravityFieldElement<T>::UniformGravityFieldElement(Vector3<double> g_W)
    : ForceElement<T>(world_model_instance()),
      g_W_(g_W) {}

template <typename T>
VectorX<T> UniformGravityFieldElement<T>::CalcGravityGeneralizedForces(
    const systems::Context<T>& context) const {
  const MultibodyTree<T>& model = this->get_parent_tree();
  const auto& mbt_context =
      dynamic_cast<const MultibodyTreeContext<T>&>(context);

  // TODO(amcastro-tri): Get these from the cache.
  PositionKinematicsCache<T> pc(model.get_topology());
  model.CalcPositionKinematicsCache(context, &pc);
  VelocityKinematicsCache<T> vc(model.get_topology());
  vc.InitializeToZero();

  // Create a multibody forces initialized by default to zero forces.
  MultibodyForces<T> forces(model);
  // Add this element's force contributions, gravity, into the forces object.
  this->CalcAndAddForceContribution(mbt_context, pc, vc, &forces);

  // Temporary output vector of spatial forces for each body B at their inboard
  // frame Mo, expressed in the world W.
  std::vector<SpatialForce<T>> F_BMo_W_array(model.num_bodies());

  // Zero vector of generalized accelerations.
  const VectorX<T> vdot = VectorX<T>::Zero(model.num_velocities());

  // Temporary array for body accelerations.
  std::vector<SpatialAcceleration<T>> A_WB_array(model.num_bodies());

  // Output vector of generalized forces:
  VectorX<T> tau_g(model.num_velocities());

  // Compute inverse dynamics with zero generalized velocities and zero
  // generalized accelerations. Since inverse dynamics computes:
  // ID(q, v, v̇)  = M(q)v̇ + C(q, v)v - ∑ J_WBᵀ(q) Fgrav_Bo_W
  // with v = 0 and v̇ = 0 we get:
  // ID(q, v, v̇) = - ∑ J_WBᵀ(q) Fgrav_Bo_W = -tau_g(q), which is the negative of
  // the generalized forces due to gravity.
  // TODO(amcastro-tri): Replace this inverse dynamics implementation by a JᵀF
  // operator implementation, which would be more efficient.
  model.CalcInverseDynamics(
      context, pc, vc, /* state */
      VectorX<T>::Zero(model.num_velocities()), /* vdot = 0 */
      /* Applied forces. In this case only gravity. */
      forces.body_forces(), forces.generalized_forces(),
      &A_WB_array, &F_BMo_W_array, /* temporary arrays. */
      &tau_g /* Output, the generalized forces. */);
  return -tau_g;
}

template <typename T>
void UniformGravityFieldElement<T>::DoCalcAndAddForceContribution(
    const MultibodyTreeContext<T>& context,
    const PositionKinematicsCache<T>& pc,
    const VelocityKinematicsCache<T>&,
    MultibodyForces<T>* forces) const {
  // Alias to the array of applied body forces:
  std::vector<SpatialForce<T>>& F_Bo_W_array = forces->mutable_body_forces();

  // Add the force of gravity contribution for each body in the model.
  // Skip the world.
  const MultibodyTree<T>& model = this->get_parent_tree();
  const int num_bodies = model.num_bodies();
  // Skip the "world" body.
  for (BodyIndex body_index(1); body_index < num_bodies; ++body_index) {
    const Body<T>& body = model.get_body(body_index);
    BodyNodeIndex node_index = body.node_index();

    // TODO(amcastro-tri): Replace this CalcXXX() calls by GetXXX() calls once
    // caching is in place.
    const T mass = body.get_mass(context);
    const Vector3<T> p_BoBcm_B = body.CalcCenterOfMassInBodyFrame(context);
    const Matrix3<T> R_WB = pc.get_X_WB(node_index).linear();
    // TODO(amcastro-tri): Consider caching p_BoBcm_W.
    const Vector3<T> p_BoBcm_W = R_WB * p_BoBcm_B;

    const Vector3<T> f_Bcm_W = mass * gravity_vector();
    const SpatialForce<T> F_Bo_W(p_BoBcm_W.cross(f_Bcm_W), f_Bcm_W);
    F_Bo_W_array[node_index] += F_Bo_W;
  }
}

template <typename T>
T UniformGravityFieldElement<T>::CalcPotentialEnergy(
    const MultibodyTreeContext<T>& context,
    const PositionKinematicsCache<T>& pc) const {
  // Add the potential energy due to gravity for each body in the model.
  // Skip the world.
  const MultibodyTree<T>& model = this->get_parent_tree();
  const int num_bodies = model.num_bodies();
  T TotalPotentialEnergy = 0.0;
  // Skip the "world" body.
  for (BodyIndex body_index(1); body_index < num_bodies; ++body_index) {
    const Body<T>& body = model.get_body(body_index);

    // TODO(amcastro-tri): Replace this CalcXXX() calls by GetXXX() calls once
    // caching is in place.
    const T mass = body.get_mass(context);
    const Vector3<T> p_BoBcm_B = body.CalcCenterOfMassInBodyFrame(context);
    const Isometry3<T>& X_WB = pc.get_X_WB(body.node_index());
    const Matrix3<T> R_WB = X_WB.linear();
    const Vector3<T> p_WBo = X_WB.translation();
    // TODO(amcastro-tri): Consider caching p_BoBcm_W and/or p_WBcm.
    const Vector3<T> p_BoBcm_W = R_WB * p_BoBcm_B;
    const Vector3<T> p_WBcm = p_WBo + p_BoBcm_W;

    TotalPotentialEnergy -= (mass * p_WBcm.dot(gravity_vector()));
  }
  return TotalPotentialEnergy;
}

template <typename T>
T UniformGravityFieldElement<T>::CalcConservativePower(
    const MultibodyTreeContext<T>& context,
    const PositionKinematicsCache<T>& pc,
    const VelocityKinematicsCache<T>& vc) const {
  // Add the potential energy due to gravity for each body in the model.
  // Skip the world.
  const MultibodyTree<T>& model = this->get_parent_tree();
  const int num_bodies = model.num_bodies();
  T TotalConservativePower = 0.0;
  // Skip the "world" body.
  for (BodyIndex body_index(1); body_index < num_bodies; ++body_index) {
    const Body<T>& body = model.get_body(body_index);

    // TODO(amcastro-tri): Replace this CalcXXX() calls by GetXXX() calls once
    // caching is in place.
    const T mass = body.get_mass(context);
    const Vector3<T> p_BoBcm_B = body.CalcCenterOfMassInBodyFrame(context);
    const Isometry3<T>& X_WB = pc.get_X_WB(body.node_index());
    const Matrix3<T> R_WB = X_WB.linear();
    // TODO(amcastro-tri): Consider caching p_BoBcm_W.
    const Vector3<T> p_BoBcm_W = R_WB * p_BoBcm_B;

    const SpatialVelocity<T>& V_WB = vc.get_V_WB(body.node_index());
    const SpatialVelocity<T> V_WBcm = V_WB.Shift(p_BoBcm_W);
    const Vector3<T>& v_WBcm = V_WBcm.translational();

    // The conservative power is defined to be positive when the potential
    // energy decreases.
    TotalConservativePower += (mass * v_WBcm.dot(gravity_vector()));
  }
  return TotalConservativePower;
}

template <typename T>
T UniformGravityFieldElement<T>::CalcNonConservativePower(
    const MultibodyTreeContext<T>&,
    const PositionKinematicsCache<T>&,
    const VelocityKinematicsCache<T>&) const {
  // A uniform gravity field is conservative. Therefore return zero power.
  return 0.0;
}

template <typename T>
std::unique_ptr<ForceElement<double>>
UniformGravityFieldElement<T>::DoCloneToScalar(
    const MultibodyTree<double>&) const {
  return std::make_unique<UniformGravityFieldElement<double>>(gravity_vector());
}

template <typename T>
std::unique_ptr<ForceElement<AutoDiffXd>>
UniformGravityFieldElement<T>::DoCloneToScalar(
    const MultibodyTree<AutoDiffXd>&) const {
  return std::make_unique<UniformGravityFieldElement<AutoDiffXd>>(
      gravity_vector());
}

// Explicitly instantiates on the most common scalar types.
template class UniformGravityFieldElement<double>;
template class UniformGravityFieldElement<AutoDiffXd>;

}  // namespace multibody
}  // namespace drake
