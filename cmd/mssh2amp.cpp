/* Copyright (c) 2008-2017 the MRtrix3 contributors.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * MRtrix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * For more details, see http://www.mrtrix.org/.
 */


#include <sstream>

#include "command.h"
#include "math/SH.h"
#include "image.h"
#include "dwi/gradient.h"


using namespace MR;
using namespace App;


void usage ()
{
  AUTHOR = "Daan Christiaens (daan.christiaens@kcl.ac.uk) & David Raffelt (david.raffelt@florey.edu.au)";

  SYNOPSIS = "Evaluate the amplitude of an image of spherical harmonic functions along specified directions";

  ARGUMENTS
    + Argument ("input",
                "the input image consisting of spherical harmonic (SH) "
                "coefficients.").type_image_in ()
    + Argument ("gradient",
                "the gradient encoding along which the SH functions will "
                "be sampled (directions + shells)").type_file_in ()
    + Argument ("output",
                "the output image consisting of the amplitude of the SH "
                "functions along the specified directions.").type_image_out ();

  OPTIONS
    + Option ("transform",
              "rigid transformation, applied to the gradient table.")
      + Argument("T").type_file_in()

    + Option ("nonnegative",
              "cap all negative amplitudes to zero")

    + Stride::Options
    + DataType::options();
}


using value_type = float;


class MSSH2Amp { MEMALIGN(MSSH2Amp)
  public:
    template <class MatrixType>
    MSSH2Amp (const MatrixType& dirs, const size_t lmax, const vector<size_t>& idx, bool nonneg) :
      transformer (dirs.template cast<value_type>(), lmax),
      bidx (idx),
      nonnegative (nonneg),
      sh (transformer.n_SH()),
      amp (transformer.n_amp()) { }
    
    void operator() (Image<value_type>& in, Image<value_type>& out) {
      sh = in.row (4);
      transformer.SH2A(amp, sh);
      if (nonnegative)
        amp = amp.cwiseMax(value_type(0.0));
      for (size_t j = 0; j < amp.size(); j++) {
        out.index(3) = bidx[j];
        out.value() = amp[j];
      }
    }

  private:
    const Math::SH::Transform<value_type> transformer;
    const vector<size_t>& bidx;
    const bool nonnegative;
    Eigen::Matrix<value_type, Eigen::Dynamic, 1> sh, amp;
};


size_t get_bidx (vector<default_type> bvals, default_type min, default_type max)
{
  size_t idx = 0;
  for (auto b : bvals) {
    if ((b >= min - 1.0) && (b <= max + 1.0))
      return idx;
    else
      idx++;
  }
  VAR(bvals);
  VAR(min);
  VAR(max);
  throw Exception ("b-value not found.");
}



void run ()
{
  auto mssh = Image<value_type>::open(argument[0]);
  if (mssh.ndim() != 5)
    throw Exception("5-D MSSH image expected.");

  Header header (mssh);
  auto bvals = parse_floats (header.keyval().find("shells")->second);

  Eigen::MatrixXd grad;
  grad = load_matrix(argument[1]);
  DWI::Shells shells (grad);

  transform_type T;
  T.setIdentity();
  auto opt = get_options("transform");
  if (opt.size())
    T = load_transform(opt[0][0]);

  grad.block(0,0,grad.rows(),3) = grad.block(0,0,grad.rows(),3) * T.rotation().transpose();



//  if (!directions.rows())
//    throw Exception ("no directions found in input directions file");

//  std::stringstream dir_stream;
//  for (ssize_t d = 0; d < directions.rows() - 1; ++d)
//    dir_stream << directions(d,0) << "," << directions(d,1) << "\n";
//  dir_stream << directions(directions.rows() - 1,0) << "," << directions(directions.rows() - 1,1);
//  amp_header.keyval().insert(std::pair<std::string, std::string> ("directions", dir_stream.str()));

  header.ndim() = 4;
  header.size(3) = grad.rows();
  Stride::set_from_command_line (header, Stride::contiguous_along_axis (3));
  header.datatype() = DataType::from_command_line (DataType::Float32);

  auto amp_data = Image<value_type>::create(argument[2], header);

  for (size_t k = 0; k < shells.count(); k++) {
    mssh.index(3) = get_bidx(bvals, shells[k].get_min(), shells[k].get_max());
    auto directions = DWI::gen_direction_matrix (grad, shells[k].get_volumes());
    MSSH2Amp mssh2amp (directions, Math::SH::LforN (mssh.size(4)),
                       shells[k].get_volumes(), get_options("nonnegative").size());
    ThreadedLoop("computing amplitudes", mssh, 0, 3, 2).run(mssh2amp, mssh, amp_data);
  }



  
}

