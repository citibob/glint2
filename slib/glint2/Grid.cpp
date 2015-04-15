/*
 * GLINT2: A Coupling Library for Ice Models and GCMs
 * Copyright (c) 2013 by Robert Fischer
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <set>
#include <algorithm>
#include <glint2/Grid.hpp>
#include <giss/ncutil.hpp>
#include <boost/bind.hpp>
#include <giss/constant.hpp>

namespace glint2 {



/** See Surveyor's Formula: http://www.maa.org/pubs/Calc_articles/ma063.pdf */
extern double area_of_polygon(Cell const &cell)
{
	double ret = 0;
	auto it0 = cell.begin();
	auto it1(it0); ++it1;
	for(; it1 != cell.end(); it0=it1, it1 += 1) {
		ret += (it0->x * it1->y) - (it1->x * it0->y);
	}
	it1 = cell.begin();
	ret += (it0->x * it1->y) - (it1->x * it0->y);
	ret *= .5;
	return ret;
}

/** Computes area of the cell's polygon after it's projected
(for cells in lon/lat coordinates)
See Surveyor's`g Formula: http://www.maa.org/pubs/Calc_articles/ma063.pdf */
extern double area_of_proj_polygon(Cell const &cell, giss::Proj2 const &proj)
{
	double ret = 0;
	double x00, y00;
	auto it0 = cell.begin();
	proj.transform(it0->x, it0->y, x00, y00);

	double x0, y0, x1, y1;
	x0 = x00; y0 = y00;
	for(++it0; it0 != cell.end(); ++it0) {
		double x1, y1;
		proj.transform(it0->x, it0->y, x1, y1);

		ret += (x0 * y1) - (x1 * y0);
		x0 = x1;
		y0 = y1;
	}

	x1 = x00;
	y1 = y00;
	ret += (x0 * y1) - (x1 * y0);
	ret *= .5;
	return ret;
}

// ------------------------------------------------------------
Grid::Grid(Type _type) :
	type(_type),
	coordinates(Grid::Coordinates::LONLAT),
	parameterization(Grid::Parameterization::L0),
	_max_realized_cell_index(0),
	_max_realized_vertex_index(0) {}

long Grid::ndata() const
{
	if (parameterization == Parameterization::L1) {
		return nvertices_full();
	}
	return ncells_full();
}

void Grid::centroid(long ix, double &x, double &y) const
{
	switch(parameterization.index()) {
		case Grid::Parameterization::L0 : {
			/** Compute the center of a polygon in a plane.
			NOTE: Does NOT work (correctly) for lon/lat coordinates
			http://stackoverflow.com/questions/5271583/center-of-gravity-of-a-polygon */
			Cell const *cell = get_cell(ix);
			double A = area_of_polygon(*cell);
			double Cx = 0;
			double Cy = 0;
			auto v0(cell->begin());
			auto v1(v0);
			for (++ v1; v1 != cell->end(); v0=v1,++v1) {
				double cross = (v0->x * v1->y - v1->x * v0->y);
				Cx += (v0->x + v1->x) * cross;
				Cy += (v0->y + v1->y) * cross;
			}

			{	// Do it for the last segment as well!
				auto v1(cell->begin());
				double cross = (v0->x * v1->y - v1->x * v0->y);
				Cx += (v0->x + v1->x) * cross;
				Cy += (v0->y + v1->y) * cross;
			}


			double fact = 1.0 / (6.0 * A);
			x= Cx * fact;
			y = Cy * fact;
		} break;
		case Grid::Parameterization::L1 : {
			Vertex const *vertex = get_vertex(ix);
			x = vertex->x;
			y = vertex->y;
		} break;
	}
}

void Grid::clear()
{
	_vertices.clear();
	_cells.clear();
}

Cell *Grid::add_cell(Cell &&cell) {
	// If we never specify our indices, things will "just work"
	if (cell.index == -1) cell.index = _cells.size();
	_max_realized_cell_index = std::max(_max_realized_cell_index, cell.index);

	auto ret = _cells.insert(cell.index, std::move(cell));
	Cell *valp = ret.first;
	bool inserted = ret.second;

	if (!inserted) {		// Key already existed
		fprintf(stderr, "Error adding repeat cell index=%d.  "
			"Cells must have unique indices.", cell.index);
		throw std::exception();
	}
	return valp;
}

Vertex *Grid::add_vertex(Vertex &&vertex) {
	// If we never specify our indices, things will "just work"
	if (vertex.index == -1) vertex.index = _vertices.size();
	_max_realized_vertex_index = std::max(_max_realized_vertex_index, vertex.index);

	auto ret = _vertices.insert(vertex.index, std::move(vertex));
	Vertex *valp = ret.first;
	bool inserted = ret.second;

	if (!inserted) {		// Key already existed
		fprintf(stderr, "Error adding repeat vertex index=%d.  "
			"Vertices must have unique indices.", vertex.index);
		throw std::exception();
	}
	return valp;
}

// ------------------------------------------------------------
struct CmpVertexXY {
	bool operator()(Vertex const *a, Vertex const *b)
	{
		double diff = a->x - b->x;
		if (diff < 0) return true;
		if (diff > 0) return false;
		return (a->y - b->y) < 0;
	}
};

void Grid::sort_renumber_vertices()
{
	// Construct array of Vertex pointers
	std::vector<Vertex *> vertices;
	for (auto vertex = vertices_begin(); vertex != vertices_end(); ++vertex)
		vertices.push_back(&*vertex);

	// Sort it by x and y!
	std::sort(vertices.begin(), vertices.end(), CmpVertexXY());

	// Renumber vertices
	long i=0;
	for (auto vertex = vertices.begin(); vertex != vertices.end(); ++vertex)
		(*vertex)->index = i++;
}

// ------------------------------------------------------------
void Grid::netcdf_write(NcFile *nc, std::string const &vname) const
{
	// ---------- Write out the vertices
	NcVar *vertices_index_var = nc->get_var((vname + ".vertices.index").c_str());
	NcVar *vertices_xy_var = nc->get_var((vname + ".vertices.xy").c_str());

	std::vector<Vertex *> vertices(_vertices.sorted());	// Sort by index
	int i=0;
	for (auto vertex = vertices.begin(); vertex != vertices.end(); ++i, ++vertex) {
		vertices_index_var->set_cur(i);
		vertices_index_var->put(&(*vertex)->index, 1);

		double point[2] = {(*vertex)->x, (*vertex)->y};
		vertices_xy_var->set_cur(i, 0);
		vertices_xy_var->put(point, 1, 2);
	}

	// -------- Write out the cells (and vertex references)
	NcVar *cells_index_var = nc->get_var((vname + ".cells.index").c_str());
//	NcVar *cells_i_var = nc->get_var((vname + ".cells.i").c_str());
//	NcVar *cells_j_var = nc->get_var((vname + ".cells.j").c_str());
//	NcVar *cells_k_var = nc->get_var((vname + ".cells.k").c_str());
	NcVar *cells_ijk_var = nc->get_var((vname + ".cells.ijk").c_str());
	NcVar *cells_area_var = nc->get_var((vname + ".cells.area").c_str());
//	NcVar *cells_native_area_var = nc->get_var((vname + ".cells.native_area").c_str());
//	NcVar *cells_proj_area_var = nc->get_var((vname + ".cells.proj_area").c_str());

	NcVar *cells_vertex_refs_var = nc->get_var((vname + ".cells.vertex_refs").c_str());
	NcVar *cells_vertex_refs_start_var = nc->get_var((vname + ".cells.vertex_refs_start").c_str());

	std::vector<Cell *> cells(_cells.sorted());
	int ivref = 0;
	i=0;
	for (auto celli = cells.begin(); celli != cells.end(); ++i, ++celli) {
		Cell *cell(*celli);

		// Write general cell contents
		cells_index_var->set_cur(i);
		cells_index_var->put(&cell->index, 1);

		int ijk[3] = {cell->i, cell->j, cell->k};
		cells_ijk_var->set_cur(i, 0);
		cells_ijk_var->put(ijk, 1, 3);

		cells_area_var->set_cur(i);
		cells_area_var->put(&cell->area, 1);

		// Write vertex indices for this cell
		cells_vertex_refs_start_var->set_cur(i);
		cells_vertex_refs_start_var->put(&ivref, 1);
		for (auto vertex = cell->begin(); vertex != cell->end(); ++vertex) {
			cells_vertex_refs_var->set_cur(ivref);
			cells_vertex_refs_var->put(&vertex->index, 1);
			++ivref;
		}
	}

	// Write out a sentinel for polygon index bounds
	cells_vertex_refs_start_var->set_cur(i);
	cells_vertex_refs_start_var->put(&ivref, 1);
}



boost::function<void ()> Grid::netcdf_define(NcFile &nc, std::string const &vname) const
{

	// ------ Attributes
	auto one_dim = giss::get_or_add_dim(nc, "one", 1);
	NcVar *info_var = nc.add_var((vname + ".info").c_str(), ncInt, one_dim);
		info_var->add_att("version", 1);		// File format versioning
		info_var->add_att("name", name.c_str());
		info_var->add_att("type", type.str());
		info_var->add_att("type.comment",
			giss::ncwrap("The overall type of grid, controlling the C++ class used to represent the grid.  See Grid::Type in slib/glint2/Grid.hpp").c_str());

		info_var->add_att("coordinates", coordinates.str());
		info_var->add_att("coordinates.comment",
			giss::ncwrap("The coordinate system used to represent grid vertices (See Grid::Coordinates in slib/glint2/Grid.hpp.  May be either XY or LONLAT (longitude comes before latitude).  Note that this is different from grid.info.type.  A GENERIC grid, for example, could be expressed in either XY or LONLAT coordinates.").c_str());

		info_var->add_att("parameterization", parameterization.str());
		info_var->add_att("parameterization.comment",
			giss::ncwrap("Indicates how values are interpolated between grid points (See Grid::Parameterization in  slib/glint2/Grid.hpp).  Most finite difference models will use L0, while finite element models would use L1 or something else.").c_str());

		if (coordinates == Coordinates::XY) {
			info_var->add_att("projection", sproj.c_str());
			info_var->add_att("projection.comment",
				giss::ncwrap("If grid.info.coordinates = XY, this indicates the projection used to convert local XY coordinates to LONLAT coordinates on the surface of the earth.  See http://trac.osgeo.org/proj/Proj.4 for format of these strings.").c_str());

//			giss::Proj proj(sproj);
//			giss::Proj llproj(proj.latlong_from_proj());
//			info_var->add_att("llprojection", llproj.get_def().c_str());
		}

		char buf[32];
		sprintf(buf, "%ld", ncells_full());
		info_var->add_att("cells.num_full", buf);
		info_var->add_att("cells.num_full.comment",
			giss::ncwrap("The total theoretical number of grid cells (polygons) in this grid.  Depending on grid.info:parameterization, either cells or vertices will correspond to the dimensionality of the grid's vector space.").c_str());

		info_var->add_att("vertices.num_full", (int)nvertices_full());
		info_var->add_att("vertices.num_full.comment",
			giss::ncwrap("The total theoretical of vertices (of polygons) on this grid.").c_str());

	// ------- Dimensions
	// Count the number of times a vertex (any vertex) is referenced.
	int nvref = 0;
	for (auto cell = cells_begin(); cell != cells_end(); ++cell) {
		nvref += cell->size();
	}

	NcDim *nvertices_dim = nc.add_dim(
		(vname + ".vertices.num_realized").c_str(), nvertices_realized());
	info_var->add_att((vname + ".vertices.num_realized.comment").c_str(),
		giss::ncwrap("The number of 'realized' cells in this grid.  Only the outlines of realized cells are computed and stored.  not all cells need to be realized.  For example, a grid file representing a GCM grid, in preparation for use with ice models, would only need to realize GCM grid cells that are close to the relevant ice sheets.  In this case, all grid cells are realized.").c_str());

	NcDim *ncells_dim = nc.add_dim(
		(vname + ".cells.num_realized").c_str(), ncells_realized());
	NcDim *ncells_plus_1_dim = nc.add_dim(
		(vname + ".cells.num_realized_plus1").c_str(), ncells_realized() + 1);
	NcDim *nvrefs_dim = nc.add_dim(
		(vname + ".cells.num_vertex_refs").c_str(), nvref);
	NcDim *two_dim = giss::get_or_add_dim(nc, "two", 2);
	NcDim *three_dim = giss::get_or_add_dim(nc, "three", 3);

	// --------- Variables
	NcVar *ncvar;
	ncvar = nc.add_var((vname + ".vertices.index").c_str(), ncInt, nvertices_dim);
	ncvar->add_att("comment",
		giss::ncwrap("For grids that index on cells (eg, L0): a dense, zero-based 1D index used to identify each realized cell.  This will be used for vectors representing fields on the grid.").c_str());
	nc.add_var((vname + ".vertices.xy").c_str(), ncDouble, nvertices_dim, two_dim);
	ncvar = nc.add_var((vname + ".cells.index").c_str(), ncInt, ncells_dim);
	ncvar->add_att("comment",
		giss::ncwrap("For grids that index on vertices (eg, L1): a dense, zero-based 1D index used to identify each realized vertex.  This will be used for vectors representing fields on the grid.").c_str());
	ncvar = nc.add_var((vname + ".cells.ijk").c_str(), ncInt, ncells_dim, three_dim);
	ncvar->add_att("comment",
		giss::ncwrap("OPTIONAL: Up to 3 dimensions can be used to assign a 'real-world' index to each grid cell.  If grid.info:type = EXCHANGE, then i and j correspond to grid.vertices.index of the two overlapping source cells.").c_str());

	nc.add_var((vname + ".cells.area").c_str(), ncDouble, ncells_dim);
//	nc.add_var((vname + ".cells.native_area").c_str(), ncDouble, ncells_dim);
//	nc.add_var((vname + ".cells.proj_area").c_str(), ncDouble, ncells_dim);

	nc.add_var((vname + ".cells.vertex_refs").c_str(), ncInt, nvrefs_dim);
	nc.add_var((vname + ".cells.vertex_refs_start").c_str(), ncInt, ncells_plus_1_dim);

	return boost::bind(&Grid::netcdf_write, this, &nc, vname);
}

/** @param fname Name of file to load from (eg, an overlap matrix file)
@param vname Eg: "grid1" or "grid2" */
void Grid::read_from_netcdf(
NcFile &nc,
std::string const &vname)
{
	clear();

	// ---------- Read the Basic Info
	NcVar *info_var = nc.get_var((vname + ".info").c_str());
		name = std::string(giss::get_att(info_var, "name")->as_string(0));

		type = giss::parse_enum<decltype(type)>(
			giss::get_att(info_var, "type")->as_string(0));
		coordinates = giss::parse_enum<decltype(coordinates)>(
			giss::get_att(info_var, "coordinates")->as_string(0));
		parameterization = giss::parse_enum<decltype(parameterization)>(
			giss::get_att(info_var, "parameterization")->as_string(0));

		if (coordinates == Coordinates::XY)
			sproj = std::string(giss::get_att(info_var, "projection")->as_string(0));
		else
			sproj = "";

		char *sncells_full = giss::get_att(info_var, "cells.num_full")->as_string(0);
		sscanf(sncells_full, "%ld", &_ncells_full);

		_nvertices_full = giss::get_att(info_var, "vertices.num_full")->as_int(0);


	// ---------- Read the Vertices
	// Basic Info
	std::vector<int> vertices_index(
		giss::read_int_vector(nc, vname + ".vertices.index"));

	// Read points 2-d array as single vector (double)
	NcVar *vertices_xy_var = nc.get_var((vname + ".vertices.xy").c_str());
	long npoints = vertices_xy_var->get_dim(0)->size();
	std::vector<double> vertices_xy(npoints*2);
	vertices_xy_var->get(&vertices_xy[0], npoints, 2);

	// Assemble into vertices
	for (size_t i=0; i < vertices_index.size(); ++i) {
		long index = vertices_index[i];
		double x = vertices_xy[i*2];
		double y = vertices_xy[i*2 + 1];
		add_vertex(Vertex(x, y, index));
	}

	// ---------- Read the Cells
	std::vector<int> cells_index(giss::read_int_vector(nc, vname + ".cells.index"));

	NcVar *cells_ijk_var = nc.get_var((vname + ".cells.ijk").c_str());
	long ncells = cells_ijk_var->get_dim(0)->size();
	std::vector<int> cells_ijk(ncells*3);
	cells_ijk_var->get(&cells_ijk[0], ncells, 3);

	std::vector<double> cells_area(giss::read_double_vector(nc, vname + ".cells.area"));

	std::vector<int> vrefs(giss::read_int_vector(nc, vname + ".cells.vertex_refs"));
	std::vector<int> vrefs_start(giss::read_int_vector(nc, vname + ".cells.vertex_refs_start"));


	// Assemble into Cells
	for (size_t i=0; i < cells_index.size(); ++i) {
		long index = cells_index[i];

		Cell cell;
		cell.index = cells_index[i];
		cell.i = cells_ijk[i*3 + 0];
		cell.j = cells_ijk[i*3 + 1];
		cell.k = cells_ijk[i*3 + 2];

		cell.area = cells_area[i];

		// Add the vertices
		cell.reserve(vrefs_start[i+1] - vrefs_start[i]);
		for (int j = vrefs_start[i]; j < vrefs_start[i+1]; ++j)
			cell.add_vertex(get_vertex(vrefs[j]));

		// Add thecell to the grid
		add_cell(std::move(cell));
	}
}

void Grid::to_netcdf(std::string const &fname)
{
	NcFile nc(fname.c_str(), NcFile::Replace);

	// Define stuff in NetCDF file
	printf("Defining netCDF file %s\n", fname.c_str());
	auto gridd = netcdf_define(nc, "grid");

	// Write stuff in NetCDF file
	printf("Writing to netCDF file: %s\n", fname.c_str());
	gridd();

	nc.close();
}

// ---------------------------------------------------
static double const nan = std::numeric_limits<double>::quiet_NaN();

std::vector<double> Grid::get_native_areas() const
{
	// Get the cell areas
	std::vector<double> area(this->ncells_full(), nan);
	for (auto cell = this->cells_begin(); cell != this->cells_end(); ++cell) {
		area.at(cell->index) = cell->area;
	}

	return area;
}

void Grid::get_ll_to_xy(giss::Proj2 &proj, std::string const &sproj) const
{
printf("get_ll_to_xy(sproj=%s)\n", sproj.c_str());
	// Set up the projection
	if (coordinates == Coordinates::LONLAT) {
		proj.init(sproj, giss::Proj2::Direction::LL2XY);
	} else {
		fprintf(stderr, "get_ll_to_xy() only makes sense for grids in Lon/Lat Coordinates!");
		throw std::exception();
	}
}

void Grid::get_xy_to_ll(giss::Proj2 &proj, std::string const &sproj) const
{
printf("get_xy_to_ll(sproj=%s)\n", sproj.c_str());
	// Set up the projection
	if (coordinates == Coordinates::LONLAT) {
		proj.init(sproj, giss::Proj2::Direction::XY2LL);
	} else {
		fprintf(stderr, "get_xy_to_ll() only makes sense for grids in Lon/Lat Coordinates!");
		throw std::exception();
	}
}


std::vector<double> Grid::get_proj_areas(std::string const &sproj) const
{
	giss::Proj2 proj;
	get_ll_to_xy(proj, sproj);

	// Get the projected cell areas
	std::vector<double> area(this->ncells_full(), nan);
	for (auto cell = this->cells_begin(); cell != this->cells_end(); ++cell) {
		area.at(cell->index) = area_of_proj_polygon(*cell, proj);
	}

	return area;
}
// ---------------------------------------------------
/** Remove cells and vertices not relevant to us --- for example, not in our MPI domain. */
void Grid::filter_cells(boost::function<bool (int)> const &include_cell)
{
	std::set<int> good_vertices;	// Remove vertices that do NOT end up in this set.

printf("BEGIN filter_cells(%s) %p\n", name.c_str(), this);

	// Set counts so they won't change
	_ncells_full = ncells_full();
	_nvertices_full = nvertices_full();

	// Remove cells that don't fit our filter
	_max_realized_cell_index = -1;
	for (auto cell = cells_begin(); cell != cells_end(); ) { //++cell) {
		bool keep = include_cell(cell->index);
		if (keep) {
			_max_realized_cell_index = std::max(_max_realized_cell_index, cell->index);

			// Make sure we don't delete this cell's vertices
			for (auto vertex = cell->begin(); vertex != cell->end(); ++vertex)
				good_vertices.insert(vertex->index);
			++cell;
		} else {
			// Remove the cell, maybe remove its vertices later
			// Careful with iterators: invalidated after erase()
			cell = _cells.erase(cell);	// Increments too
		}
	}

	// Remove vertices that don't fit our filter
	_max_realized_vertex_index = -1;
	for (auto vertex = vertices_begin(); vertex != vertices_end(); ) {
		if (good_vertices.find(vertex->index) != good_vertices.end()) {
			_max_realized_vertex_index = std::max(_max_realized_vertex_index, vertex->index);
			++vertex;
		} else {
			// Careful with iterators: invalidated after erase()
			vertex = _vertices.erase(vertex);	// Increments too
		}
	}

	printf("END filter_cells(%s) %p\n", name.c_str(), this);
}

}	// namespace
