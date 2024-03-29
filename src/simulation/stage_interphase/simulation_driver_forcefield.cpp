#include <algorithm>

#include <md.hpp>

#include "simulation_driver.hpp"


void
simulation_driver::setup_forcefield()
{
    setup_repulsive_forcefield();
    setup_connectivity_forcefield();
    setup_loop_forcefield();
    setup_nucleolus_forcefield();
    setup_membrane_forcefield();
    setup_context();
}


void
simulation_driver::setup_repulsive_forcefield()
{
    // General A/B-type particle repulsions.

    auto const max_diameter = std::max(
        _config.a_core_diameter,
        _config.b_core_diameter
    );

    _system.add_forcefield(
        md::make_neighbor_pairwise_forcefield(
            [this](md::index i, md::index j) {
                auto const data = _system.view(particle_data_attribute);
                auto const a = 0.5 * (data[i].a_factor + data[j].a_factor);
                auto const b = 0.5 * (data[i].b_factor + data[j].b_factor);

                md::softcore_potential<2, 3> const a_potential {
                    .energy   = _config.a_core_repulsion,
                    .diameter = _config.a_core_diameter * _context.core_scale,
                };
                md::softcore_potential<8, 3> const b_potential {
                    .energy   = _config.b_core_repulsion,
                    .diameter = _config.b_core_diameter * _context.core_scale,
                };

                return a * a_potential + b * b_potential;
            }
        )
        .set_neighbor_distance([=, this] {
            return max_diameter * _context.core_scale;
        })
    );
}


void
simulation_driver::setup_connectivity_forcefield()
{
    // Chromosome polymer connectivity.

    auto chrom_bonds = _system.add_forcefield(
        md::make_bonded_pairwise_forcefield(
            [this](md::index i, md::index j) {
                auto const data = _system.view(particle_data_attribute);
                auto const a = 0.5 * (data[i].a_factor + data[j].a_factor);
                auto const b = 0.5 * (data[i].b_factor + data[j].b_factor);

                // Bond parameters vary on the type of the bonded cores. Just
                // mix the parameters if the types of the bonded cores are
                // not the same.
                auto const K =
                    a * _config.a_core_bond_spring +
                    b * _config.b_core_bond_spring;
                auto const l =
                    a * _config.a_core_bond_length +
                    b * _config.b_core_bond_length;

                // The spring constant K corresponds to the inverse-variance of
                // the fluctuation. If we scale the bond length, the fluctuation
                // should also be scaled.
                auto const s = _context.bond_scale;
                auto const scaled_K = K * (1 / (s * s));
                auto const scaled_l = l * s;

                return md::semispring_potential {
                    .spring_constant      = scaled_K,
                    .equilibrium_distance = scaled_l,
                };
            }
        )
    );

    for (auto const& chain : _design.chains) {
        chrom_bonds->add_bonded_range(chain.start, chain.end);
    }
}


void
simulation_driver::setup_loop_forcefield()
{
    // Mean-field intra-TAD loops.

    auto loop_bonds = _system.add_forcefield(
        md::make_bonded_pairwise_forcefield(
            [this](md::index i, md::index j) {
                auto const data = _system.view(particle_data_attribute);
                auto const a = 0.5 * (data[i].a_factor + data[j].a_factor);
                auto const b = 0.5 * (data[i].b_factor + data[j].b_factor);

                // Bond parameters vary on the type of the bonded cores. Just
                // mix the parameters if the types of the bonded cores are
                // not the same.
                auto const K =
                    a * _config.a_core_2nd_bond_spring +
                    b * _config.b_core_2nd_bond_spring;

                // The spring constant K corresponds to the inverse-variance of
                // the fluctuation. If we scale the bond length, the fluctuation
                // should also be scaled.
                auto const s = _context.bond_scale;
                auto const scaled_K = K * (1 / (s * s));

                return md::harmonic_potential {
                    .spring_constant = scaled_K,
                };
            }
        )
    );

    for (auto const& chain : _design.chains) {
        for (auto i = chain.start; i + 2 < chain.end; i++) {
            loop_bonds->add_bonded_pair(i, i + 2);
        }
    }
}


void
simulation_driver::setup_nucleolus_forcefield()
{
    // Nucleolar "sidechains" attached to active NORs.

    auto nucleo_bonds = _system.add_forcefield(
        md::make_bonded_pairwise_forcefield(
            [this](md::index, md::index) {
                auto const s = _context.bond_scale;
                auto const K = _config.nucleolus_bond_spring * (1 / (s * s));
                auto const b = _config.nucleolus_bond_length * s;
                return md::semispring_potential {
                    .spring_constant      = K,
                    .equilibrium_distance = b,
                };
            }
        )
    );

    for (auto const& [nor, nuc] : _design.nucleolar_bonds) {
        nucleo_bonds->add_bonded_pair(nor, nuc);
    }

    // Nucleolar droplet-forming attractive interactions. This is expensive to
    // compute, so add only when interaction energy is set to nonzero.
    if (_config.nucleolus_droplet_energy == 0) {
        return;
    }

    std::vector<md::index> nucleolar_particles;
    for (auto const& [nor, nuc] : _design.nucleolar_bonds) {
        nucleolar_particles.push_back(nuc);
    }

    _system.add_forcefield(
        md::make_neighbor_pairwise_forcefield(
            md::apply_cutoff(
                md::softwell_potential<6> {
                    .energy         = _config.nucleolus_droplet_energy,
                    .decay_distance = _config.nucleolus_droplet_decay,
                },
                _config.nucleolus_droplet_cutoff
            )
        )
        .set_neighbor_distance(_config.nucleolus_droplet_cutoff)
        .set_neighbor_targets(nucleolar_particles)
    );
}


void
simulation_driver::setup_membrane_forcefield()
{
    // Confinement into the nuclear membrane.

    auto set_ellipsoid = [this] {
        return md::ellipsoid {
            .center     = {0, 0, 0},
            .semiaxis_x = _context.wall_semiaxes.x,
            .semiaxis_y = _context.wall_semiaxes.y,
            .semiaxis_z = _context.wall_semiaxes.z,
        };
    };

    auto inward_forcefield = _system.add_forcefield(
        md::make_ellipsoid_inward_forcefield(
            // Smaller particle can get closer to the wall than larger one. So
            // inner membrane should be aware of particle type.
            [this](md::index i) {
                auto const data = _system.view(particle_data_attribute);
                auto const a = 0.5 * (data[i].a_factor + _config.wall_ab_factor.a);
                auto const b = 0.5 * (data[i].b_factor + _config.wall_ab_factor.b);

                md::softcore_potential<2, 3> const a_potential {
                    .energy   = _config.a_core_repulsion,
                    .diameter = _config.a_core_diameter / 2 * _context.core_scale,
                };
                md::softcore_potential<8, 3> const b_potential {
                    .energy   = _config.b_core_repulsion,
                    .diameter = _config.b_core_diameter / 2 * _context.core_scale,
                };

                return a * a_potential + b * b_potential;
            }
        )
        .set_ellipsoid(set_ellipsoid)
    );

    // The inward forcefield confines the particles in the nucleus. However,
    // some particles may go outside due to fluctuation. This outward
    // forcefield ensures the confinement.
    auto outward_forcefield = _system.add_forcefield(
        md::make_ellipsoid_outward_forcefield(
            md::harmonic_potential {
                .spring_constant = _config.wall_packing_spring,
            }
        )
        .set_ellipsoid(set_ellipsoid)
    );

    _compute_packing_reaction = [=] {
        auto const inward = inward_forcefield->stats.axial_reaction;
        auto const outward = outward_forcefield->stats.axial_reaction;
        return inward + outward;
    };
}
