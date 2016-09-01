# define a replacement macro for calling ../configure instead of ./configure
%global dconfigure %(printf %%s '%configure' | sed 's!\./configure!../configure!g')

# preamble #####################################################################
# shared
Name:           fdistdump
Version:        0.1.0
Release:        1%{?dist}
Summary:        Distributed IP flow files processing tool

License:        GPLv2+
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
BuildRequires:  libnf-devel
BuildRequires:  openmpi-devel
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
BuildRequires:  libnf-devel
BuildRequires:  mpich-devel
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
%dconfigure --program-suffix="${MPI_SUFFIX}" ;\
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
%{_mandir}/man1/fdistdump.1.gz

%files openmpi
%{_bindir}/fdistdump_openmpi

%files mpich
%{_bindir}/fdistdump_mpich


# changelog section ############################################################
%changelog
* Tue Aug 30 2016 Jan Wrona <wrona@cesnet.cz> - 0.1.0-1
- First vesrion of the specfile.
