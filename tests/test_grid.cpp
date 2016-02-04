// https://github.com/google/googletest/blob/master/googletest/docs/Primer.md

#include <gtest/gtest.h>
#include <icebin/Grid.hpp>
#include <iostream>
#include <cstdio>
#include <netcdf>

using namespace icebin;
using namespace netCDF;

#if 0
bool operator==(Vertex const &a, Vertex const &b)
{
	return  ((a.index == b.index) && (a.x == b.x) && (a.y == b.y));
}

bool operator==(Cell const &a, Cell const &b)
{
	if (a.size() != b.size()) return false;

	if ((a.index != b.index) || (a.native_area != b.native_area) || (a.i != b.i) || (a.j != b.j) || (a.k != b.k)) return false;

	for (auto iia(a.begin()), iib(b.begin()); iia != a.end(); ++iia, ++iib) {
		if (!(*iia == *iib)) return false;
	}
	return true;
}

bool operator==(Grid const &a, Grid const &b)
{

	std::vector<Cell const *> acells(a.cells.sorted());
	std::vector<Cell const *> bcells(b.cells.sorted());
	if (acells.size() != bcells.size()) return false;
	for (auto iia(acells.begin()), iib(bcells.begin());
		iia != acells.end(); ++iia, ++iib)
	{
std::cout << "Comparing: " << **iia << " --- " << **iib << std::endl;
std::cout << "        == " << (**iia == **iib) << std::endl;
		if (!(**iia == **iib)) return false;
	}

	std::vector<Vertex const *> avertices(a.vertices.sorted());
	std::vector<Vertex const *> bvertices(b.vertices.sorted());
	if (avertices.size() != bvertices.size()) return false;
	for (auto iia(avertices.begin()), iib(bvertices.begin());
		iia != avertices.end(); ++iia, ++iib)
	{
std::cout << "Comparing: " << **iia << " --- " << **iib << std::endl;
		if (!(**iia == **iib)) return false;
	}

	return true;
}
#endif


// The fixture for testing class Foo.
class GridTest : public ::testing::Test {
protected:

	std::vector<std::string> tmpfiles;

	// You can do set-up work for each test here.
	GridTest() {}

	// You can do clean-up work that doesn't throw exceptions here.
	virtual ~GridTest()
	{
		for (auto ii(tmpfiles.begin()); ii != tmpfiles.end(); ++ii) {
//			::remove(ii->c_str());
		}
	}

	// If the constructor and destructor are not enough for setting up
	// and cleaning up each test, you can define the following methods:

	// Code here will be called immediately after the constructor (right
	// before each test).
	virtual void SetUp() {}

	// Code here will be called immediately after each test (right
	// before the destructor).
	virtual void TearDown() {}

//	  // The mock bar library shaed by all tests
//	  MockBar m_bar;


	void expect_eq(Vertex const &a, Vertex const &b)
	{
		EXPECT_EQ(a.index, b.index);
		EXPECT_EQ(a.x, b.x);
		EXPECT_EQ(a.y, b.y);
	}

	void expect_eq(Cell const &a, Cell const &b)
	{
		EXPECT_EQ(a.size(), b.size());

		EXPECT_EQ(a.index, b.index);
		EXPECT_EQ(a.native_area, b.native_area);
		EXPECT_EQ(a.i, b.i);
		EXPECT_EQ(a.j, b.j);
		EXPECT_EQ(a.k, b.k);

		for (auto iia(a.begin()), iib(b.begin()); iia != a.end(); ++iia, ++iib) {
			expect_eq(*iia, *iib);
		}
	}

	void expect_eq(Grid const &a, Grid const &b)
	{

		std::vector<Cell const *> acells(a.cells.sorted());
		std::vector<Cell const *> bcells(b.cells.sorted());
		EXPECT_EQ(acells.size(), bcells.size());
		for (auto iia(acells.begin()), iib(bcells.begin());
			iia != acells.end(); ++iia, ++iib)
		{
			expect_eq(**iia, **iib);
		}

		std::vector<Vertex const *> avertices(a.vertices.sorted());
		std::vector<Vertex const *> bvertices(b.vertices.sorted());
		EXPECT_EQ(avertices.size(), bvertices.size());
		for (auto iia(avertices.begin()), iib(bvertices.begin());
			iia != avertices.end(); ++iia, ++iib)
		{
			expect_eq(**iia, **iib);
		}
	}


};

TEST_F(GridTest, create_grid)
{
	Grid grid;
	grid.type = Grid::Type::XY;
	grid.name = "Test Grid";
	grid.coordinates = Grid::Coordinates::XY;
	grid.parameterization = Grid::Parameterization::L0;

	Vertex *vertex;
	auto &vertices(grid.vertices);
	vertices.add(Vertex(0,0));
	vertices.add(Vertex(1,0));
	vertices.add(Vertex(2,0));
	vertices.add(Vertex(0,1));
	vertices.add(Vertex(1,1));
	vertex = vertices.add(Vertex(2,1));

	expect_eq(*vertex, *vertex);

	auto &cells(grid.cells);
	Cell *cell;
	cell = cells.add(Cell({vertices.at(0), vertices.at(1), vertices.at(4), vertices.at(3)}));
		cell->i = 0;
		cell->j = 0;
		cell->native_area = 2.;

	cell = cells.add(Cell({vertices.at(1), vertices.at(2), vertices.at(5), vertices.at(4)}));
		cell->i = 1;
		cell->j = 0;
		cell->native_area = 3.;

	expect_eq(*cell, *cell);

	EXPECT_EQ(2., cells.at(0)->native_area);
	EXPECT_EQ(3., cells.at(1)->native_area);

	EXPECT_DOUBLE_EQ(1., cells.at(0)->proj_area(NULL));
	EXPECT_DOUBLE_EQ(1., cells.at(1)->proj_area(NULL));

	expect_eq(grid, grid);

	// ---------------- Write to NetCDF
	// If the constructor and destructor are not enough for setting up
	std::string fname("__netcdf_test.nc");
	tmpfiles.push_back(fname);
	::remove(fname.c_str());
	{
		ibmisc::NcIO ncio(fname, NcFile::replace);
		grid.ncio(ncio, "grid");
		ncio.close();
	}

	// ---------------- Read from NetCDF
	// If the constructor and destructor are not enough for setting up
	{
		Grid grid2;
		ibmisc::NcIO ncio(fname, NcFile::read);
		grid2.ncio(ncio, "grid");
		ncio.close();

		expect_eq(grid2, grid);
	}

}


int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}