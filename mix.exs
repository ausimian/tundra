defmodule Tundra.MixProject do
  use Mix.Project

  @version "0.4.1"
  @source_url "https://github.com/ausimian/tundra"

  def project do
    [
      app: :tundra,
      description: "TUN device support for Elixir",
      version: @version,
      elixir: "~> 1.18",
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      compilers: [:elixir_make] ++ Mix.compilers(),
      package: package(),
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
      {:ex_doc, "~> 0.40", only: :dev, runtime: false},
      {:publisho, "~> 1.0", only: :dev, runtime: false}
    ]
  end

  defp docs do
    [
      main: "readme",
      source_url: @source_url,
      source_ref: @version,
      logo: "tundra.png",
      extras: ["LICENSE.md", "CHANGELOG.md", "README.md"]
    ]
  end

  defp package do
    [
      description: "TUN device support for Elixir",
      licenses: ["MIT"],
      files: [
        "lib",
        "c_src",
        "mix.exs",
        "Makefile",
        "README.md",
        "LICENSE.md",
        "CHANGELOG.md",
        ".formatter.exs"
      ],
      links: %{
        "GitHub" => "#{@source_url}/tree/#{@version}"
      }
    ]
  end
end
