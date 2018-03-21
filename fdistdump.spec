# define a replacement macro for calling ../configure instead of ./configure
%global mpi_configure %(printf %%s '%configure' | sed 's!\./configure!../configure!g')

# preamble #####################################################################
# shared
Name:           fdistdump
Version:        0.4.0
Release:        1%{?dist}
Summary:        Distributed IP flow files processing tool

License:        BSD
URL:            https://github.com/CESNET/fdistdump
Source0:        %{url}/releases/download/v%{version}/%{name}-%{version}.tar.gz

Group:          Applications/Databases
Vendor:         CESNET, a.l.e.
Packager:       Jan Wrona <wrona@cesnet.cz>

%description
fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility
to apply powerful record filter.

# preamble for the common subpackage
%package        common
Summary:        Distributed IP flow files processing tool, common files
BuildArch:      noarch

%description    common
fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility
to apply powerful record filter. This package contains common, compiler
independent files.

# preamble for the Open MPI subpackage
%package        openmpi
Summary:        Distributed IP flow files processing tool, compiled against Open MPI
BuildRequires:  gcc autoconf autoconf-archive automake libtool make
BuildRequires:  libnf-devel
BuildRequires:  openmpi-devel
BuildRequires:  bloom_filter_indexes
# require explicitly to guarantee the pickup of the right runtime
Requires:       openmpi
Requires:       %{name}-common = %{version}-%{release}

%description    openmpi
fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility
to apply powerful record filter. This package is compiled against Open MPI.

# preamble for the MPICH subpackage
%package        mpich
Summary:        Distributed IP flow files processing tool, compiled against MPICH
BuildRequires:  gcc autoconf autoconf-archive automake libtool make
BuildRequires:  libnf-devel
BuildRequires:  mpich-devel
BuildRequires:  bloom_filter_indexes
# require explicitly to guarantee the pickup of the right runtime
Requires:       mpich
Requires:       %{name}-common = %{version}-%{release}

%description    mpich
fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility
to apply powerful record filter. This package is compiled against MPICH.


# shared prep section ##########################################################
%prep
%setup -q


# shared build section #########################################################
%build
%define mpi_build() \
mkdir "${MPI_COMPILER}"; \
cd "${MPI_COMPILER}"; \
%mpi_configure --bindir="${MPI_BIN}" --program-suffix="${MPI_SUFFIX}" ;\
make %{?_smp_mflags} ; \
cd ..

# build for the Open MPI subpackage
%{_openmpi_load}
%mpi_build
%{_openmpi_unload}

# build for the MPICH subpackage
%{_mpich_load}
%mpi_build
%{_mpich_unload}

# configure for the common subpackage (only because of man pages)
# this has to be last and arbitrary MPI has to be loaded
%{_openmpi_load}
%configure
%{_openmpi_unload}


# shared install section #######################################################
%install
rm -rf $RPM_BUILD_ROOT

# install files for the common subpackage
make install-man DESTDIR=%{buildroot}

# install files for the Open MPI subpackage
%{_openmpi_load}
make --directory="${MPI_COMPILER}" install uninstall-man DESTDIR=%{buildroot}
%{_openmpi_unload}

# install files for the MPICH subpackage
%{_mpich_load}
make --directory="${MPI_COMPILER}" install uninstall-man DESTDIR=%{buildroot}
%{_mpich_unload}


# files section ################################################################
%files common
%doc README.md TODO
%{_mandir}/man1/%{name}.1.gz

%files openmpi
%{_libdir}/openmpi/bin/%{name}_openmpi

%files mpich
%{_libdir}/mpich/bin/%{name}_mpich


# changelog section ############################################################
%changelog
* Wed Mar 21 2018 Jan Wrona <wrona@cesnet.cz> - 0.4.0-1
- minor version bump (0.3.1 -> 0.4.0)

* Tue Feb 20 2018 Jan Wrona <wrona@cesnet.cz> - 0.3.1-1
- patch version bump (0.3.0 -> 0.3.1)

* Mon Jan 22 2018 Jan Wrona <wrona@cesnet.cz> - 0.3.0-1
- Incremented the minor version number (0.2.2 -> 0.3.0).
- Change the license from GPL to BSD.

* Sun Jan 21 2018 Jan Wrona <wrona@cesnet.cz> - 0.2.2-2
- Fix missing build dependencies: gcc autoconf automake libtool make.
- Add new build dependency: bloom_filter_indexes. Support for Bloom filter base
  indexes has been integrated into fdistdump using bloom-filter-index library
  (https://github.com/CESNET/bloom-filter-index).

* Thu Jul 27 2017 Jan Wrona <wrona@cesnet.cz> - 0.2.2-1
- Incremented the patch version number (0.2.1 -> 0.2.2).

* Tue Sep 6 2016 Jan Wrona <wrona@cesnet.cz> - 0.2.1-1
- Changed the patch version number.

* Tue Sep 6 2016 Jan Wrona <wrona@cesnet.cz> - 0.2.0-1
- Changed the minor version number.

* Thu Sep 1 2016 Jan Wrona <wrona@cesnet.cz> - 0.1.0-2
- Changed install paths, because MPI implementation specific files MUST be
  installed in the directories used by the used MPI compiler.

* Tue Aug 30 2016 Jan Wrona <wrona@cesnet.cz> - 0.1.0-1
- First vesrion of the specfile.
