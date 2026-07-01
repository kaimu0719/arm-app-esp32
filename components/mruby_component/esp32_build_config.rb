MRuby::Build.new do |conf|
  conf.toolchain :gcc

  conf.cc.command       = '/usr/bin/cc'
  conf.cxx.command      = '/usr/bin/c++'
  conf.linker.command   = '/usr/bin/cc'
  conf.archiver.command = '/usr/bin/ar'

  conf.gembox 'default'
end

MRuby::CrossBuild.new('esp32') do |conf|
  conf.toolchain :gcc

  conf.cc do |cc|
    if ENV['COMPONENT_INCLUDES']
      cc.include_paths += ENV['COMPONENT_INCLUDES'].split(';')
    end

    cc.flags << '-mlongcalls'
    cc.defines << 'ESP_PLATFORM'
  end

  conf.bins = []
  conf.build_mrbtest_lib_only
  conf.disable_cxx_exception

  conf.gem core: 'mruby-compiler'
end
