# config valid only for current version of Capistrano
lock '3.4.1'

set :application, 'kuzanbo_lock_auth'
set :repo_url, 'git@github.com:shallontecbiz/kuzanbo_lock_auth.git'

# Default branch is :master
# ask :branch, `git rev-parse --abbrev-ref HEAD`.chomp
ask :branch, `git rev-parse --abbrev-ref HEAD`.chomp

set :keep_releases, 3

# Default deploy_to directory is /var/www/my_app_name
# set :deploy_to, '/var/www/my_app_name'
set :deploy_to, "/home/ec2-user/node_apps/kuzanbo_lock_auth"

set :rbenv_type, :user
set :rbenv_ruby, '2.6.5'
set :rbenv_path, "/usr/local/rbenv"
set :rbenv_prefix, "RBENV_ROOT=#{fetch(:rbenv_path)} RBENV_VERSION=#{fetch(:rbenv_ruby)} #{fetch(:rbenv_path)}/bin/rbenv exec"
set :rbenv_map_bins, %w{rake gem bundle ruby}
set :rbenv_roles, :all # default value

set :user, "ec2-user"
set :group, "ec2-user"

set :ssh_options, {
  user: 'ec2-user',
  forward_agent: true
}

set :nvm_type, :user # or :system, depends on your nvm setup
set :nvm_node, 'v8.9.4'
set :nvm_map_bins, %w{node npm forever}

set :npm_flags, '--production --silent --no-progress'
set :npm_roles, :all
set :npm_env_variables, {}

set :linked_dirs, %w{node_modules}

set :node_env, (fetch(:node_env) || fetch(:stage))
set :default_env, { node_env: fetch(:node_env) }

set :web_path, current_path

# Default value for :scm is :git
# set :scm, :git

# Default value for :format is :pretty
# set :format, :pretty

# Default value for :log_level is :debug
# set :log_level, :debug

# Default value for :pty is false
# set :pty, true

# Default value for :linked_files is []
# set :linked_files, fetch(:linked_files, []).push('config/database.yml', 'config/secrets.yml')

# Default value for linked_dirs is []
# set :linked_dirs, fetch(:linked_dirs, []).push('log', 'tmp/pids', 'tmp/cache', 'tmp/sockets', 'vendor/bundle', 'public/system')

# Default value for default_env is {}
# set :default_env, { path: "/opt/ruby/bin:$PATH" }

# Default value for keep_releases is 5
# set :keep_releases, 5

namespace :deploy do
  desc 'Start application'
  task :start do
    on roles(:app) do
      within fetch(:web_path) do
        execute :forever, 'start',  'index.js'
      end
    end
  end
 
  desc 'Stop application'
  task :stop do
    on roles(:app) do
      within fetch(:web_path) do
        execute :forever, 'stopall'
      end
    end
  end
 
  desc 'Restart application'
  task :restart do
    on roles(:app), in: :sequence, wait: 5 do
      within fetch(:web_path) do
        execute :forever, 'stopall'
        execute :forever, 'start', 'index.js'
      end
    end
  end
  after :publishing, :restart
end
