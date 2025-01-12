defmodule Tundra.MixProject do
  use Mix.Project

  def project do
    [
      app: :tundra,
      version: version(),
      elixir: "~> 1.15",
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      compilers: [:elixir_make] ++ Mix.compilers(),
      docs: docs()
    ]
  end

  # Run "mix help compile.app" to learn about applications.
  def application do
    [
      registered: [Tundra.DynamicSupervisor],
      extra_applications: [:logger],
      mod: {Tundra.Application, []}
    ]
  end

  # Run "mix help deps" to learn about dependencies.
  defp deps do
    [
      {:elixir_make, "~> 0.9", runtime: false},
      {:typedstruct, "~> 0.5", runtime: false},
      {:ex_doc, "~> 0.36", only: :dev, runtime: false}
    ]
  end

  defp docs do
    [
      main: "README",
      source_url: "https://github.com/ausimian/tundra",
      source_ref: "#{version()}",
      logo: "tundra.png",
      extras: ["LICENSE.md", "CHANGELOG.md", "README.md"]
    ]
  end

  defp version do
    version_from_pkg() || version_from_git() || "0.0.0"
  end

  defp version_from_pkg do
    if File.exists?("./hex_metadata.config") do
      {:ok, info} = :file.consult("./hex_metadata.config")
      Map.new(info)["version"]
    end
  end

  defp version_from_git do
    case System.cmd("git", ["describe", "--tags", "--abbrev=0"], [stderr_to_stdout: true]) do
      {version, 0} -> String.trim(version)
      _ -> nil
    end
  end
end
