filename = System.argv() |> Enum.at(0)

data_dirs = [
  "hpgmg_mpi_8_8_bolt_no_preemption",
  "hpgmg_mpi_8_8_bolt_10000",
  "hpgmg_mpi_8_8_bolt_1000",
  "hpgmg_mpi_8_8_iomp",
]

opts = [
  {"BOLT (nonpreemptive)"    , "#1f77b4", "dot"    , "square-open" },
  {"BOLT (preemptive; 10 ms)", "#ff7f0e", "dashdot", "diamond-open"},
  {"BOLT (preemptive; 1 ms)" , "#2ca02c", "solid"  , "circle"      },
  {"IOMP"                    , "#d62728", "dash"   , "x-thin"      },
]

baseline_dir = "hpgmg_mpi_8_8_bolt_fixed"

n_workers = :lists.seq(4, 28, 1)
# n_workers = :lists.seq(4, 28, 2)

linewidth = 3

get_data_in_file = fn path ->
  [_ | times] =
    File.stream!(path)
    |> Stream.map(&String.split/1)
    |> Stream.map(fn
      ["FMGSolve...", _, _, _, _, "(" <> time, _] -> Float.parse(time) |> elem(0)
      _ -> nil
    end)
    |> Stream.filter(&!is_nil(&1))
    |> Enum.to_list()
    |> Enum.take(11)
  times
end

get_data_in_dir = fn path ->
  Enum.map(n_workers, fn w ->
    times =
      "#{path}/w_#{w}_*.out"
      |> Path.expand()
      |> Path.wildcard()
      |> Enum.map(get_data_in_file)
      |> List.flatten()
    {w, times}
  end)
end

baseline_times =
  get_data_in_dir.(baseline_dir)
  |> Enum.sort()
  |> Enum.map(fn {_, ts} -> Statistics.mean(ts) end)

Enum.zip(data_dirs, opts)
|> Enum.map(fn {dir, opt} ->
  {title, color, dash, symbol} = opt
  {xs, times} =
    get_data_in_dir.(dir)
    |> Enum.sort()
    |> Enum.unzip()
  {ys, errors} =
    Enum.zip(times, baseline_times)
    |> Enum.map(fn {ts, b} -> Enum.map(ts, fn t -> t / b - 1 end) end)
    |> Enum.map(fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
    |> Enum.unzip()
  %{
    x: xs,
    y: ys,
    mode: 'lines+markers',
    name: title,
    error_y: %{
      type:      "data",
      array:     errors,
      visible:   true,
      thickness: linewidth,
    },
    line: %{
      width: linewidth,
      color: color,
      dash: dash,
    },
    marker: %{
      symbol: symbol,
      size:   4,
      line:   %{width: linewidth, color: color},
    },
  }
end)
|> PlotlyEx.plot(%{
  width:  600,
  height: 350,
  xaxis: %{
    showgrid: true,
    title: %{text: "# of active cores (n)"},
    showline: true,
  },
  yaxis: %{
    title: %{text: "Overhead"},
    tickformat: ",.0%",
    showline: true,
    range: [0, 0.6],
  },
  legend: %{
    x: 0,
    y: 1,
    # x: 1,
    # y: 1,
    bgcolor: "rgba(0,0,0,0)",
  },
  font: %{
    size: 20,
  },
  margin: %{
    l: 80,
    r: 10,
    b: 60,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show(filename: filename)
